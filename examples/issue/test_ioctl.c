#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

int main() {
    struct winsize w;
    
    printf("Is stdout a tty? %d\n", isatty(STDOUT_FILENO));
    
    int r = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    printf("ioctl result: %d, errno: %d, ws_row: %d, ws_col: %d\n", 
           r, errno, w.ws_row, w.ws_col);
    
    return 0;
}
