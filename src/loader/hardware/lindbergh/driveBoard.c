
#include <stdint.h>
#include <string.h>

#include "../../config/config.h"
#include "../../redirections/filesystemShared.h"
#include "driveBoard.h"
#include "forceFeedback.h"
#include "jvs.h"

#define DRIVEBOARD_READY 0x00
#define DRIVEBOARD_NOT_INIT 0x11
#define DRIVEBOARD_BUSY 0x44

extern uint32_t gId;
extern int gGrp;

unsigned char response;
int wheelInitialized;

uint8_t buffer[64];
uint8_t bufferIdx = 0;

int initDriveboard()
{
    response = DRIVEBOARD_NOT_INIT;
    wheelInitialized = 0;
    return 0;
}

int enableRead = 0;

ssize_t driveboardRead(int fd, void *buf, size_t count)
{
    memset(buffer, '\0', 64);
    if (enableRead)
    {
        char *mybuf = (char *)buf;
        mybuf[0] = response;
        enableRead = 0;
    }
    return 1;
}

ssize_t driveboardWrite(int fd, const void *buf, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)buf;
    for (size_t i = 0; i < count; ++i)
    {
        buffer[bufferIdx++] = bytes[i];
        if (bufferIdx == 4)
        {
            // if (buffer[0] != 0xfd)// && buffer[0] != 0x00 && buffer[0] != 0xff && buffer[0] != 0x01 && buffer[0] != 0xfa)
            // {
            //     printf("Write: fd: %d,count: %zu, bufferIdx: %d\n", fd, count, bufferIdx);
            //     for (int x = 0; x < 4; x++)
            //     {
            //         printf("0x%02x ", buffer[x]);
            //     }
            //     printf("\n");
            // }
            processDrivePacket(buffer, 1);
            sdlFfbDriveboard(buffer, 4);
            if (count != 7)
                bufferIdx = 0;
        }
        else if (bufferIdx == 7)
        {
            uint8_t player2Buffer[4];
            player2Buffer[0] = buffer[0];
            player2Buffer[1] = buffer[4];
            player2Buffer[2] = buffer[5];
            player2Buffer[3] = buffer[6];
            processDrivePacket(player2Buffer, 2);
            // if (player2Buffer[0] != 0xfd && player2Buffer[0] != 0x00 && player2Buffer[0] != 0xff && player2Buffer[0] != 0x01)
            // {
                // printf("Write P2: fd: %d,count: %zu, bufferIdx: %d\n", fd, count, bufferIdx);
                // for (int x = 0; x < 4; x++)
                // {
                //     printf("0x%02x ", player2Buffer[x]);
                // }
                // printf("\n");
            // }
            bufferIdx = 0;
        }
    }
    return count;
}

void processDrivePacket(uint8_t *buf, int player)
{
    static int fcP1 = 0;
    static int fcP2 = 0;

    switch (buf[0])
    {
        case 0xFF:
        {
            printf("Driveboard: Drive board reset\n");
            // response = 0x88;
            response = DRIVEBOARD_READY;
        }
        break;

        case 0x81:
        {
            printf("Driveboard: Drive board reset 2\n");
            response = DRIVEBOARD_NOT_INIT;
        }
        break;

        case 0xFC:
        {
            printf("Driveboard: Start wheel bounds testing\n");
            if (player == 1)
            {
                if (fcP1 == 0)
                {
                    response = DRIVEBOARD_BUSY;
                    fcP1++;
                    break;
                }
            }
            else if (player == 2)
            {
                if (fcP2 == 0)
                {
                    response = DRIVEBOARD_BUSY;
                    fcP2++;
                    break;
                }
            }
            response = DRIVEBOARD_READY;
        }
        break;

        case 0x80:
        {
            if ((buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x00) || (buf[1] == 0x01 && buf[2] == 0x00 && buf[3] == 0x01) ||
                (buf[1] == 0x01 && buf[2] == 0x00 && buf[3] == 0x00))
                wheelInitialized = 1;
            response = DRIVEBOARD_READY;
        }
        break;

        case 0x83:
        case 0x86:
        {
            response = DRIVEBOARD_READY;
        }
        break;

        case 0x9e:
        {
            response = DRIVEBOARD_READY;
        }
        break;
        case 0x84:
        {
            response = DRIVEBOARD_READY;

            if ((buf[1] == 0x01 && buf[2] == 0x00 && buf[3] == 0x05) && !wheelInitialized)
            {
                if (player == 1)
                    setAnalogue(ANALOGUE_1, (int)(0.5f * 1024.0f));
                else
                    setAnalogue(ANALOGUE_5, (int)(0.5f * 1024.0f));
                break;
            }

            uint8_t cmd1 = buf[1];
            if (gGrp == GROUP_OUTRUN_TEST)
            {
                if (buf[4] != 0)
                    cmd1 = buf[4];
            }

            float scaled = 0.0f;
            if (cmd1 == 1)
                scaled = (128.0f - (buf[2] - 1)) / 256.0f;

            if (cmd1 == 0)
                scaled = (256.0f - (buf[2] + 1)) / 256.0f;

            if (!wheelInitialized)
            {
                if (player == 1)
                    setAnalogue(ANALOGUE_1, (int)(scaled * 1024.0f));
                else
                    setAnalogue(ANALOGUE_5, (int)(scaled * 1024.0f));
            }
        }
        break;

        case 0xF1:
        {
            response = DRIVEBOARD_READY;
        }
        break;

        case 0xFA:
        case 0xFD:
        {
            response = DRIVEBOARD_READY;
        }
        break;

        default:
            // printf("Driveboard: Unknown command received %X\n", buffer[0]);
            break;
    }

    enableRead = 1;
}

int driveBoardioctl(int fd, unsigned int request, void *data)
{
    if (enableRead)
    {
        uint8_t d = 1;
        memcpy(data, &d, sizeof(uint8_t));
        return 0;
    }
    return -1;
}
