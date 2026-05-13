#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cardReader.h"
#include "../../config/config.h"
#include "../../patching/flowControl.h"
#include "../../log/log.h"
#include "../../mainShared.h"

#define BLOCKSIZE 8

extern uint32_t gId;
extern int gGrp;
bool cardReaderInitialized = false;

void (*idWriteFileHeader)(void *) = NULL;

uint8_t cardHeader[] = {0x08, 0x00, 0x00, 0x00, 0x81, 0x00, 0x59, 0xda, 0x00, 0x00, 0x54, 0x4d, 0x50, 0x06, 0x03, 0x23, 0x10, 0x41, 0x62,
                        0xad, 0x00, 0x2b, 0x0b, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x95, 0x71, 0x52, 0x70, 0x00, 0x00,
                        0x00, 0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0xd2};

uint8_t cardHeaderVf5[] = {0x08, 0x00, 0x00, 0x00, 0xE5, 0xE7, 0x55, 0xC5, 0x00, 0x00, 0x54, 0x4D, 0x50, 0x10, 0x07, 0x13, 0x10, 0x41, 0x89,
                           0x23, 0x00, 0x26, 0x04, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x95, 0x71, 0x55, 0x40, 0x00, 0x00,
                           0x00, 0x01, 0xAA, 0xAA, 0xAA, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x70, 0x17};

SerialFD serialFD[2];
WriteCommand writeCmd[2];
ReadCommand readCmd[2];

void initReadCmd(int fdIdx)
{
    readCmd[fdIdx].cmd = 0;
    readCmd[fdIdx].cmdCompleted = false;
    readCmd[fdIdx].cmdLen = 0;
    readCmd[fdIdx].sentBytes = 0;
    memset(&readCmd[fdIdx].sendBuffer, '\0', 256);
}

void initWriteCmd(int fdIdx)
{
    writeCmd[fdIdx].cmdCompleted = false;
    writeCmd[fdIdx].packetLen = 0;
    writeCmd[fdIdx].recBytes = 0;
    memset(&writeCmd[fdIdx].recBuffer, '\0', sizeof(uint8_t));
}

int initCardReader()
{
    EmulatorConfig *config = getConfig();

    if (gGrp != GROUP_VT3 && gGrp != GROUP_VT3_TEST && gId != R_TUNED_SBQW && gGrp != GROUP_VF5)
    {
        log_warn("Warning: This Game does not support Card Reader, I will disable the card reader for you.\n");
        config->emulateHW210CardReader = 0;
        return 0;
    }

    if (config->emulateHW210CardReader && config->emulateDriveboard && gId != R_TUNED_SBQW)
    {
        log_warn("Warning: This Game does not support Card Reader and Driver Board emulation enabled at the same time.\n");
        log_warn("Please disable Drive Board emulations in the config file.\n");
        return -1;
    }

    initReadCmd(0);
    initReadCmd(1);
    initWriteCmd(0);
    initWriteCmd(1);
    cardReaderInitialized = true;
    return 0;
}

int carReaderGetFdIdx(int fd)
{
    return (fd == serialFD[0].fd) ? 0 : 1;
}

uint8_t cardReaderGenChecksum(int fdIdx)
{
    int checksum = 0;
    for (int i = 0; i < readCmd[fdIdx].cmdLen - 1; i++)
    {
        checksum ^= readCmd[fdIdx].sendBuffer[i];
    }
    return checksum;
}

void buildOutputBuffer(int fdIdx, uint8_t cmd)
{
    uint16_t packetLen = 0;

    readCmd[fdIdx].sendBuffer[0] = 0x10;
    readCmd[fdIdx].sendBuffer[1] = cmd;
    switch (cmd)
    {
        case 0x10:
        {
            readCmd[fdIdx].sendBuffer[5] = 0x20;
        }
        case 0x11:
        {
            if (writeCmd[fdIdx].recBuffer[5] == 0x04)
            {
                initWriteCmd(fdIdx);
                return;
            }
        }
        case 0x14:
        case 0x15:
        case 0x20:
        {
            if (cmd == 0x20 && writeCmd[fdIdx].recBuffer[5] == 0x00)
                readCmd[fdIdx].sendBuffer[4] = 0x80;
        }
        case 0x25: // VF5?
        case 0x26:
        case 0x27:
        {
            packetLen = 0x02;
            readCmd[fdIdx].sendBuffer[3] = packetLen;
        }
        break;
        case 0x21:
        {
            packetLen = 0x0a;
            readCmd[fdIdx].sendBuffer[3] = packetLen;
            readCmd[fdIdx].sendBuffer[6] = 0x81;
            readCmd[fdIdx].sendBuffer[8] = 0x59;
            readCmd[fdIdx].sendBuffer[9] = 0xda;
        }
        break;
        case 0x22:
        {
            packetLen = 0x0a;
            readCmd[fdIdx].sendBuffer[3] = packetLen;
            readCmd[fdIdx].sendBuffer[7] = 0x08;
        }
        break;
        case 0x33:
        {
            packetLen = 0x0a;
            readCmd[fdIdx].sendBuffer[3] = packetLen;
            readCmd[fdIdx].sendBuffer[6] = 0xff;
            readCmd[fdIdx].sendBuffer[7] = 0xff;
        }
        break;
        case 0x34:
        {
            uint16_t blockStart = (writeCmd[fdIdx].recBuffer[6] << 8) | writeCmd[fdIdx].recBuffer[7];
            uint16_t blocksToRead = (writeCmd[fdIdx].recBuffer[8] << 8) | writeCmd[fdIdx].recBuffer[9];
            packetLen = (BLOCKSIZE * blocksToRead) + 2;
            readCmd[fdIdx].sendBuffer[3] = packetLen;
            FILE *crdFile = fopen(serialFD[fdIdx].cardFilename, "rb");
            fseek(crdFile, BLOCKSIZE * blockStart, SEEK_SET);
            fread(readCmd[fdIdx].sendBuffer + 6, 1, packetLen - 2, crdFile);
            fclose(crdFile);
        }
        break;
        case 0x35:
        {
            readCmd[fdIdx].sendBuffer[3] = 0x02;
            uint16_t blockStart = (writeCmd[fdIdx].recBuffer[6] << 8) | writeCmd[fdIdx].recBuffer[7];
            uint16_t blocksToWrite = (writeCmd[fdIdx].recBuffer[8] << 8) | writeCmd[fdIdx].recBuffer[9];
            packetLen = BLOCKSIZE * blocksToWrite;
            FILE *crdFile = fopen(serialFD[fdIdx].cardFilename, "rb+");
            fseek(crdFile, BLOCKSIZE * blockStart, SEEK_SET);
            fwrite(writeCmd[fdIdx].recBuffer + 10, 1, packetLen, crdFile);
            fclose(crdFile);
            packetLen = 0x02;
        }
        break;
        default:
        {
            log_error("Command @0x%x not supported.\n", cmd);
        }
    }
    readCmd[fdIdx].cmdLen = packetLen + 5;
    readCmd[fdIdx].sendBuffer[readCmd[fdIdx].cmdLen - 1] = cardReaderGenChecksum(fdIdx);
}

size_t cardReaderWrite(int fd, const void *buf, size_t count)
{
    if (!cardReaderInitialized)
        return -1;

    int fdIdx = carReaderGetFdIdx(fd);
    if (writeCmd[fdIdx].cmdCompleted)
    {
        log_error("Something went wrong, a write command was issued after the command was completed.\n");
    }

    if (writeCmd[fdIdx].recBytes == 4)
    {
        writeCmd[fdIdx].packetLen = (writeCmd[fdIdx].recBuffer[2] << 8) | writeCmd[fdIdx].recBuffer[3];
    }

    writeCmd[fdIdx].recBuffer[writeCmd[fdIdx].recBytes] = *((uint8_t *)buf);
    writeCmd[fdIdx].recBytes++;

    if (writeCmd[fdIdx].recBytes == writeCmd[fdIdx].packetLen + 5)
    {
        writeCmd[fdIdx].cmdCompleted = true;
        initReadCmd(fdIdx);
        buildOutputBuffer(fdIdx, writeCmd[fdIdx].recBuffer[1]);
    }
    return count;
}

size_t cardReaderRead(int fd, void *buf, size_t count)
{
    if (!cardReaderInitialized)
        return -1;

    int fdIdx = carReaderGetFdIdx(fd);
    if (!writeCmd[fdIdx].cmdCompleted || readCmd[fdIdx].cmdCompleted)
        return -1;

    if (readCmd[fdIdx].cmdLen == readCmd[fdIdx].sentBytes)
    {
        readCmd[fdIdx].cmdCompleted = true;
        initWriteCmd(fdIdx);
        return -1;
    }

    int remaining = readCmd[fdIdx].cmdLen - readCmd[fdIdx].sentBytes;

    if (remaining <= 0)
    {
        readCmd[fdIdx].cmdCompleted = true;
        initWriteCmd(fdIdx);
        return -1;
    }

    int toSend = remaining < count ? remaining : count;

    memcpy(buf, &readCmd[fdIdx].sendBuffer[readCmd[fdIdx].sentBytes], toSend);

    readCmd[fdIdx].sentBytes += toSend;

    return toSend;
}

void cardReaderSetFd(int serial, int fd, char *fName)
{
    serialFD[serial].fd = fd;
    serialFD[serial].cardFilename = fName;

    if (access(fName, F_OK) != 0)
    {
        FILE *crdFile = fopen(fName, "wb+");
        if (getConfig()->crc32 == R_TUNED_SBQW)
        {
            cardHeader[0x22] = 0x02;
            cardHeader[0x23] = 0x60;
        }
        if (gGrp == GROUP_VF5)
            fwrite(cardHeaderVf5, 1, 56, crdFile);
        else
            fwrite(cardHeader, 1, 56, crdFile);
        uint8_t z = 0;
        fseek(crdFile, 0x7c7, SEEK_CUR);
        fwrite(&z, 1, 1, crdFile);
        fclose(crdFile);
        log_info("Cardfile %s created.\n", fName);
    }
}

void idPatchEject()
{
    switch (gId)
    {
        case INITIALD_4_EXP_SBNK_REVB:
        {
            patchMemoryFromString(0x082939f9, "84"); // tickInsertWait
            patchMemoryFromString(0x08293a2b, "85");
            patchMemoryFromString(0x0819251e, "eb"); // reqRead
        }
        break;
        case INITIALD_4_EXP_SBNK_REVC:
        {
            patchMemoryFromString(0x08293aa9, "84"); // tickInsertWait
            patchMemoryFromString(0x08293adb, "85");
            patchMemoryFromString(0x081925ce, "eb"); // reqRead
        }
        break;
        case INITIALD_4_EXP_SBNK_REVD:
        {
            patchMemoryFromString(0x08295479, "84"); // tickInsertWait
            patchMemoryFromString(0x082954ab, "85");
            patchMemoryFromString(0x0819349e, "eb"); // reqRead
        }
        break;
        case INITIALD_4_SBML_REVA:
        {
            patchMemoryFromString(0x0828149d, "85"); // tickInsertWait
            patchMemoryFromString(0x08281603, "85");
            patchMemoryFromString(0x0817943e, "eb"); // reqRead
        }
        break;
        case INITIALD_4_SBML_REVB:
        {
            patchMemoryFromString(0x082815c5, "85"); // tickInsertWait
            patchMemoryFromString(0x0828172b, "85");
            patchMemoryFromString(0x081793ce, "eb"); // reqRead
        }
        break;
        case INITIALD_4_SBML_REVC:
        {
            patchMemoryFromString(0x08281ae9, "85"); // tickInsertWait
            patchMemoryFromString(0x08281c51, "85");
            patchMemoryFromString(0x0817990c, "eb"); // reqRead
        }
        break;
        case INITIALD_4_SBML_REVD:
        {
            patchMemoryFromString(0x082833dd, "85"); // tickInsertWait
            patchMemoryFromString(0x08283543, "85");
            patchMemoryFromString(0x08179c1c, "eb"); // reqRead
        }
        break;
        case INITIALD_4_SBML_REVG:
        {
            patchMemoryFromString(0x082a4ae5, "84"); // tickInsertWait
            patchMemoryFromString(0x082a4b17, "85");
            patchMemoryFromString(0x08190cbe, "eb"); // reqRead
        }
        break;
        case INITIALD_5_EXP_SBRY:
        {
            patchMemoryFromString(0x083e1eb9, "84"); // tickInsertWait
            patchMemoryFromString(0x083e1eeb, "85");
        }
        break;
        case INITIALD_5_EXP20_SBTS:
        {
            patchMemoryFromString(0x083ebd1b, "84"); // tickInsertWait
            patchMemoryFromString(0x083ebd51, "85");
        }
        break;
        case INITIALD_5_EXP20_SBTS_REVA:
        {
            patchMemoryFromString(0x083ec07b, "84"); // tickInsertWait
            patchMemoryFromString(0x083ec0b1, "85");
        }
        break;
        case INITIALD_5_JAP_SBQZ_REVA:
        {
            patchMemoryFromString(0x083d92bd, "84"); // tickInsertWait
            patchMemoryFromString(0x083d92f1, "85");
        }
        break;
        case INITIALD_5_JAP_SBQZ_REVF:
        {
            patchMemoryFromString(0x083ec6b9, "84"); // tickInsertWait
            patchMemoryFromString(0x083ec6eb, "85");
        }
    }
}

void idWriteFileHeaderTram(void *param1)
{
    idPatchEject();
    idWriteFileHeader(param1);
}

bool idCardFileExists(const char *folderPath, long expectedSize, bool twoDigits)
{
    if (folderPath != NULL && strlen(folderPath) > 0)
    {
        struct stat buffer;
        stat(folderPath, &buffer);
        if (!S_ISDIR(buffer.st_mode))
        {
            log_error("ID Card Folder does not exist! Check the config file.");
            exit(EXIT_FAILURE);
        }
    }

    char fileName[20];
    char currentFullPath[1024];
    int maxCards = 999;
    char *cardFilename = "InidCrd%03d.crd";

    if (twoDigits)
    {
        maxCards = 99;
        cardFilename = "InidCard%02d.crd";
    }

    for (int i = 0; i <= maxCards; ++i)
    {
        sprintf(fileName, cardFilename, i);
        if (folderPath != NULL && strlen(folderPath) > 0)
        {
            char lastChar = folderPath[strlen(folderPath) - 1];
            if (lastChar != PATH_SEPARATOR)
                snprintf(currentFullPath, sizeof(currentFullPath), "%s%c%s", folderPath, PATH_SEPARATOR, fileName);
            else
                snprintf(currentFullPath, sizeof(currentFullPath), "%s%s", folderPath, fileName);
        }
        else
        {
            snprintf(currentFullPath, sizeof(currentFullPath), "%s", fileName);
        }

        if (strlen(currentFullPath) >= sizeof(currentFullPath))
        {
            fprintf(stderr, "Error: Constructed path for '%s' in folder '%s' is too long.\n", fileName, folderPath);
            continue;
        }

        struct stat fileStat;
        if (stat(currentFullPath, &fileStat) == 0)
        {
            if (fileStat.st_size == expectedSize)
            {
                return true;
            }
        }
    }
    // Card file not found, we disable the autoload.
    getConfig()->idCardFileAutoload = false;
    return false;
}