#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <fcntl.h>

const char TORCH_ON = '1';

FILE *z_ang_vel_fd;
FILE *torch_fd;
char *z_delta_buff;
char *torch_stat_buff;

/* static int strength = 4000; */
static int strength = 8000;
static int duration = 300; //in milliseconds

void cleanup()
{
    fclose(z_ang_vel_fd);
    fclose(torch_fd);
    free(z_delta_buff);
    free(torch_stat_buff);
}

int exit_error(char *msg) {
    fprintf(stderr, "%s\n", msg);
    fprintf(stderr, "%d: %s\n", errno, strerror(errno));
    cleanup();
    return EXIT_FAILURE;
}

// Shamelessly ripped off from here: https://git.sr.ht/~proycon/clickclack/tree/master/item/clickclack.c#L176
// Thanks goes to Maarten van Gompel!
void vibrate() {
    int fd, ret;
    int effects;

    fd = open("/dev/input/by-path/platform-vibrator-event", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Error reading opening event device /dev/input/by-path/platform-vibrator-event\n");
        return;
    }

    if (ioctl(fd, EVIOCGEFFECTS, &effects) < 0) {
        fprintf(stderr, "EVIOCGEFFECTS failed\n");
        close(fd);
        return;
    }

    struct ff_effect e = {
        .type = FF_RUMBLE,
        .id = -1,
        .u.rumble = { .strong_magnitude = strength },
    };

    if (ioctl(fd, EVIOCSFF, &e) < 0) {
        fprintf(stderr, "EVIOCSFF failed\n");
        close(fd);
        return;
    }

    struct input_event play = { .type = EV_FF, .code = e.id, .value = 3 };
    if (write(fd, &play, sizeof play) < 0) {
        fprintf(stderr, "write failed\n");
        close(fd);
        return;
    }

    usleep(duration * 1000);

    if (ioctl(fd, EVIOCRMFF, e.id) < 0) {
        fprintf(stderr, "EVIOCRMFF failed\n");
        close(fd);
        return;
    }

    close(fd);
}

int main(int argc, char **argv) {
    struct timespec sleep_time = {0, 50000000};
    /* clock_t start = clock(); */
    clock_t first_shake = 0;
    clock_t bottomed = 0;

    // TODO: Find a way to dynamically look this path up so that it doesn't break when 
    // udev assignes the device to a different file.
    if ((z_ang_vel_fd = fopen("/sys/bus/iio/devices/iio:device2/in_anglvel_z_raw", "r")) == 0) {
        return exit_error("Error accessing internal gyro.");
    }

    if ((torch_fd = fopen("/sys/class/leds/white:flash/brightness", "r+")) == 0) {
        return exit_error("Error accessing internal torch led.");
    }
    
    z_delta_buff = malloc(256);
    torch_stat_buff = malloc(1);
    long z_delta = 0;

    while (true) {
        if (fread(z_delta_buff, 1, 256, z_ang_vel_fd) == 0) {
            return exit_error("Error reading internal gyro position.");
        }
        z_delta = strtol(z_delta_buff, NULL, 10);
        /* printf("%ld\t%ld\n", (clock() - start) / 1000, z_delta); */
        fflush(stdout);
        if (first_shake) {
            clock_t current_time = clock();
            if (current_time - first_shake < 5000) {
                if (z_delta > 6000 && bottomed) {
                    first_shake = 0;
                    bottomed = 0;
                    printf("%s", "Change");
                    if (fread(torch_stat_buff, 1, 1, torch_fd) == 0) {
                        return exit_error("Error reading internal torch state.");
                    }
                    fseek(torch_fd, 0, SEEK_SET);
                    char torch_write_stat[2] = "1\n";
                    if (torch_stat_buff[0] == TORCH_ON) {
                        torch_write_stat[0] = '0';
                    }
                    printf(" %c\n", torch_write_stat[0]);
                    if (fwrite(torch_write_stat, 1, 2, torch_fd) == 0) {
                        return exit_error("Error writing internal torch state.");
                    }
                    if (torch_stat_buff[0] == '0') {
                        vibrate();
                    } else {
                        vibrate();
                        usleep(5000);
                        vibrate();
                    }
                    fseek(torch_fd, 0, SEEK_SET);
                }
            } else {
                printf("Reset\n");
                first_shake = 0;
                bottomed = 0;
            }
        }
        if (z_delta > 15000 && !first_shake) {
            printf("First shake\n");
            first_shake = clock();
        }
        if (z_delta < 1 && first_shake) {
            bottomed = clock();
        }
        nanosleep(&sleep_time, NULL);
        // TODO: Use memset(z_delta_buff, 0, 256) if needed
        z_delta_buff[0] = '\0';
        // Seek back to the start of the file since the file is not continuous.
        // It emits a new value on each read.
        fseek(z_ang_vel_fd, 0, SEEK_SET);
    }

    cleanup();
    return EXIT_SUCCESS;
}

