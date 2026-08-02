#ifdef __linux__
#include <termios.h>
#include <dlfcn.h>

#include "../config/config.h"
#include "termios.h"
#include "filesystemShared.h"

#ifdef __linux__
#define REAL_FUNC(name) dlsym(RTLD_NEXT, #name)
#else
#define REAL_FUNC(name) name
#endif

int myTcgetattr(int fd, struct termios *termios_p)
{
    int (*_tcgetattr)(int fd, struct termios *termios_p) = REAL_FUNC(tcgetattr);

    if (fd == hooks[SERIAL0] && getConfig()->emulateDriveboard == 1)
        return 0;

    return _tcgetattr(fd, termios_p);
}

int myTcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    int (*_tcsetattr)(int fd, int optional_actions, const struct termios *termios_p) = REAL_FUNC(tcsetattr);

    if (fd == hooks[SERIAL0] && getConfig()->emulateDriveboard == 1)
        return 0;

    return _tcsetattr(fd, optional_actions, termios_p);
}

speed_t myCfgetispeed(const struct termios *termios_p)
{
    speed_t (*_cfgetispeed)(const struct termios *termios_p) = REAL_FUNC(cfgetispeed);

    if (getConfig()->emulateDriveboard == 1)
        return B9600;

    return _cfgetispeed(termios_p);
}

speed_t myCfgetospeed(const struct termios *termios_p)
{
    speed_t (*_cfgetospeed)(const struct termios *termios_p) = REAL_FUNC(cfgetospeed);

    if (getConfig()->emulateDriveboard == 1)
        return B9600;

    return _cfgetospeed(termios_p);
}

int myCfsetispeed(struct termios *termios_p, speed_t speed)
{
    int (*_cfsetispeed)(struct termios *termios_p, speed_t speed) = REAL_FUNC(cfsetispeed);

    if (getConfig()->emulateDriveboard == 1)
        return 0;

    return _cfsetispeed(termios_p, speed);
}

int myCfsetospeed(struct termios *termios_p, speed_t speed)
{
    int (*_cfsetospeed)(struct termios *termios_p, speed_t speed) = REAL_FUNC(cfsetospeed);

    if (getConfig()->emulateDriveboard == 1)
        return 0;

    return _cfsetospeed(termios_p, speed);
}

#ifdef __linux__
int tcgetattr(int fd, struct termios *termios_p)
{
    return myTcgetattr(fd, termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    return myTcsetattr(fd, optional_actions, termios_p);
}

speed_t cfgetispeed(const struct termios *termios_p)
{
    return myCfgetispeed(termios_p);
}

speed_t cfgetospeed(const struct termios *termios_p)
{
    return myCfgetospeed(termios_p);
}

int cfsetispeed(struct termios *termios_p, speed_t speed)
{
    return myCfsetispeed(termios_p, speed);
}

int cfsetospeed(struct termios *termios_p, speed_t speed)
{
    return myCfsetospeed(termios_p, speed);
}
#endif

#endif
