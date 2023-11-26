#include <sys/inotify.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <json-c/json.h>
#include <linux/input-event-codes.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_EVENTS 1024
#define LEN_NAME 16
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME ))

#define JOY_DEV "/dev/input/js"
#define UINPUT_DEV "/dev/uinput"
#define MAX_JOY 32

int buttons[8];
int axis[7];

#define KEY_UP 103
#define KEY_DOWN 108
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define ALT_KEY KEY_LEFTALT
#define TAB_KEY KEY_TAB
#define SUPER_KEY KEY_LEFTMETA
#define CTRL_KEY KEY_LEFTCTRL
#define SHIFT_KEY KEY_LEFTSHIFT
#define ENTER_KEY KEY_ENTER
#define F1_KEY KEY_F1
#define F2_KEY KEY_F2
#define ESC_KEY KEY_ESC
#define F12_KEY KEY_F12

int a_button_pressed = 0;
pthread_t a_button_thread;

int x_axis = 0;
int y_axis = 0;
int rz_axis = 0;

void send_key_event(int fd, int code1, int code2, int code3, int value) {
    struct input_event event = {0};
    event.type = EV_KEY;
    event.code = code1;
    event.value = value;
    write(fd, &event, sizeof(event));
    event.code = code2;
    write(fd, &event, sizeof(event));
    event.code = code3;
    write(fd, &event, sizeof(event));
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    write(fd, &event, sizeof(event));
}

int scan_and_open_joystick() {
    int joy_fd;
    char joy_name[128];
    char joy_dev[16];

    for (int i = 0; i < MAX_JOY; i++) {
        sprintf(joy_dev, "%s%d", JOY_DEV, i);
        joy_fd = open(joy_dev, O_RDONLY);
        if (joy_fd >= 0 && ioctl(joy_fd, JSIOCGNAME(sizeof(joy_name)), joy_name) >= 0) {
            printf("Joystick encontrado: %s\n", joy_name);
            return joy_fd;
        }
        close(joy_fd);
    }
    return -1;
}

int wait_for_joystick() {
    int joy_fd;
    while ((joy_fd = scan_and_open_joystick()) < 0) {
        sleep(5);
    }
    return joy_fd;
}

int create_uinput_device() {
    int uinput_fd = open(UINPUT_DEV, O_WRONLY);
    if (uinput_fd < 0) {
        perror("Erro ao abrir o dispositivo de entrada virtual");
        exit(1);
    }

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, SUPER_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, ALT_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, SHIFT_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, TAB_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, buttons[0]);
    ioctl(uinput_fd, UI_SET_KEYBIT, ENTER_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_UP);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_DOWN);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_LEFT);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_RIGHT);
    ioctl(uinput_fd, UI_SET_KEYBIT, CTRL_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, SHIFT_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, F1_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, F2_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, F12_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, ESC_KEY);

    struct uinput_user_dev uidev = {0};
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "uinput-joystick");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    write(uinput_fd, &uidev, sizeof(uidev));
    ioctl(uinput_fd, UI_DEV_CREATE);

    return uinput_fd;
}

void* a_button_function(void *arg) {
    usleep(1000000);
    if (a_button_pressed) {
        system("xdotool click 3");
    }
    pthread_exit(NULL);
}

void* mouse_movement_function(void *arg) {
    while (1) {
        if (x_axis != 0 || y_axis != 0) {
            char command[50];
            sprintf(command, "xdotool mousemove_relative -- %d %d", x_axis / 4000, y_axis / 4000);
            system(command);
            usleep(12000); // Ajuste este valor para controlar a velocidade do cursor
        } else if (rz_axis > 5000) {
            char command[50];
            sprintf(command, "xdotool click --repeat 1 5");
            system(command);
        } else if (rz_axis < -5000) {
            char command[50];
            sprintf(command, "xdotool click --repeat 1 4");
            system(command);
        } else {
            usleep(20000);
        }
    }
    pthread_exit(NULL);
}

void load_config(int *buttons, int *axis) {
    FILE *fp;
    char buffer[1024];
    struct json_object *parsed_json;
    struct json_object *buttons_obj;
    struct json_object *axis_obj;
    size_t n_buttons, n_axis;

    fp = fopen("config.txt","r");
    fread(buffer, 1024, 1, fp);
    fclose(fp);

    parsed_json = json_tokener_parse(buffer);

    json_object_object_get_ex(parsed_json, "buttons", &buttons_obj);
    json_object_object_get_ex(parsed_json, "axis", &axis_obj);

    n_buttons = json_object_array_length(buttons_obj);
    n_axis = json_object_array_length(axis_obj);

    for(size_t i=0; i<n_buttons; i++) {
        buttons[i] = json_object_get_int(json_object_array_get_idx(buttons_obj, i));
    }

    for(size_t i=0; i<n_axis; i++) {
        axis[i] = json_object_get_int(json_object_array_get_idx(axis_obj, i));
    }
}

void* monitor_config(void *arg) {
    int length, i = 0, wd;
    int fd;
    char buffer[BUF_LEN];

    fd = inotify_init();
    if ( fd < 0 ) {
        perror("Não foi possível inicializar o inotify");
    }

    wd = inotify_add_watch(fd, ".", IN_MODIFY | IN_CREATE | IN_DELETE);

    if (wd == -1)
    {
        printf("Não foi possível adicionar o monitoramento para %s\n", ".");
    }
    else
    {
        printf("Monitorando:: %s\n", ".");
    }

    while(1)
    {
        i = 0;
        length = read(fd, buffer, BUF_LEN);

        if (length < 0) {
            perror("read");
        }

        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            if (event->len) {
                if (event->mask & IN_MODIFY) {
                    if (event->mask & IN_ISDIR) {
                        printf("O diretório %s foi modificado.\n", event->name);
                    } else {
                        printf("O arquivo %s foi modificado.\n", event->name);
                        if(strcmp(event->name, "config.txt") == 0) {
                            printf("config.txt foi modificado, recarregando a configuração.\n");
                            load_config(buttons, axis);
                        }
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }

    inotify_rm_watch(fd, wd);
    close(fd);

    return NULL;
}

void process_joystick_events(int joy_fd, int uinput_fd) {
    struct js_event js;
    pthread_t mouse_movement_thread;
    pthread_create(&mouse_movement_thread, NULL, mouse_movement_function, NULL);

    while (1) {
        if (read(joy_fd, &js, sizeof(struct js_event)) < 0) {
            printf("Joystick desconectado\n");
            close(joy_fd);
                        joy_fd = wait_for_joystick();
            continue;
        }

        if(js.type == JS_EVENT_BUTTON) {
            if(js.number == buttons[0]) {
                a_button_pressed = js.value;
                if (js.value) {
                    pthread_create(&a_button_thread, NULL, a_button_function, NULL);
                } else {
                    pthread_join(a_button_thread, NULL);
                    send_key_event(uinput_fd, ENTER_KEY, 0, 0, 1);
                    usleep(50000);
                    send_key_event(uinput_fd, ENTER_KEY, 0, 0, 0);
                }
            } else             if(js.number == buttons[0]) {
                a_button_pressed = js.value;
                if (js.value) {
                    pthread_create(&a_button_thread, NULL, a_button_function, NULL);
                } else {
                    pthread_join(a_button_thread, NULL);
                    send_key_event(uinput_fd, ENTER_KEY, 0, 0, 1);
                    usleep(50000);
                    send_key_event(uinput_fd, ENTER_KEY, 0, 0, 0);
                }
            } else if(js.number == buttons[7]) {
                send_key_event(uinput_fd, SUPER_KEY, 0, 0, js.value);
            } else if(js.number == buttons[4]) {
                send_key_event(uinput_fd, ALT_KEY, SHIFT_KEY, TAB_KEY, js.value);
                if (js.value) {
                    usleep(50000);
                    send_key_event(uinput_fd, TAB_KEY, 0, 0, 0);
                }
            } else if(js.number == buttons[5]) {
                send_key_event(uinput_fd, ALT_KEY, TAB_KEY, 0, js.value);
                if (js.value) {
                    usleep(50000);
                    send_key_event(uinput_fd, TAB_KEY, 0, 0, 0);
                }
            } else if(js.number == buttons[2]) {
                if (js.value) {
                    system("xdotool click 1");
                }
            } else if(js.number == buttons[6]) {
                if (js.value) {
                    send_key_event(uinput_fd, CTRL_KEY, F12_KEY, 0, 1);
                    usleep(50000);
                    send_key_event(uinput_fd, CTRL_KEY, F12_KEY, 0, 0);
                }
            } else if(js.number == buttons[3]) {
                if (js.value) {
                    system("xdotool key XF86Back");
                }
            } else if(js.number == buttons[1]) {
                if (js.value) {
                    send_key_event(uinput_fd, ESC_KEY, 0, 0, 1);
                    usleep(50000);
                    send_key_event(uinput_fd, ESC_KEY, 0, 0, 0);
                }
            }
        } else if(js.type == JS_EVENT_AXIS) {
            if(js.number == axis[0]) {
                x_axis = js.value;
            } else if(js.number == axis[1]) {
                y_axis = js.value;
            } else if(js.number == axis[2]) {
                if (js.value > 0) {
                    send_key_event(uinput_fd, SUPER_KEY, CTRL_KEY, KEY_LEFT, 1);
                    usleep(50000);
                    send_key_event(uinput_fd, SUPER_KEY, CTRL_KEY, KEY_LEFT, 0);
                }
            } else if(js.number == axis[3]) {
                rz_axis = js.value;
            } else if(js.number == axis[4]) {
                if (js.value > 0) {
                    send_key_event(uinput_fd, SUPER_KEY, CTRL_KEY, KEY_RIGHT, 1);
                    usleep(50000);
                    send_key_event(uinput_fd, SUPER_KEY, CTRL_KEY, KEY_RIGHT, 0);
                }
            } else if(js.number == axis[5]) {
                if (js.value > 0) {
                    send_key_event(uinput_fd, KEY_RIGHT, 0, 0, 1);
                } else if (js.value < 0) {
                    send_key_event(uinput_fd, KEY_LEFT, 0, 0, 1);
                } else {
                    send_key_event(uinput_fd, KEY_RIGHT, 0, 0, 0);
                    send_key_event(uinput_fd, KEY_LEFT, 0, 0, 0);
                }
            } else if(js.number == axis[6]) {
                if (js.value > 0) {
                    send_key_event(uinput_fd, KEY_DOWN, 0, 0, 1);
                } else if (js.value < 0) {
                    send_key_event(uinput_fd, KEY_UP, 0, 0, 1);
                } else {
                    send_key_event(uinput_fd, KEY_DOWN, 0, 0, 0);
                    send_key_event(uinput_fd, KEY_UP, 0, 0, 0);
                }
            }
        }
    }
}

int main() {
    load_config(buttons, axis);

    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor_config, NULL);

    int joy_fd = wait_for_joystick();
    int uinput_fd = create_uinput_device();
    process_joystick_events(joy_fd, uinput_fd);
    close(joy_fd);
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    return 0;
}
