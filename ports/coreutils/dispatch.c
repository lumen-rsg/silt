#include "silt-system.h"

int silt_echo_main(int argc, char** argv);
int silt_basename_main(int argc, char** argv);
int silt_dirname_main(int argc, char** argv);

int silt_cat_main(int argc, char** argv);
int silt_head_main(int argc, char** argv);

int silt_tail_main(int argc, char** argv);
int silt_wc_main(int argc, char** argv);

int silt_pwd_main(int argc, char** argv);
int silt_printenv_main(int argc, char** argv);
int silt_yes_main(int argc, char** argv);

int main(int argc, char** argv) {
    if (argc < 1 || !argv[0]) return 1;
    set_program_name(argv[0]);
    if (strcmp(program_name, "coreutils") == 0 && argc > 1) {
        argc--;
        argv++;
        set_program_name(argv[0]);
    }
    if (strcmp(program_name, "echo") == 0) return silt_echo_main(argc, argv);
    if (strcmp(program_name, "basename") == 0) return silt_basename_main(argc, argv);
    if (strcmp(program_name, "dirname") == 0) return silt_dirname_main(argc, argv);
    if (strcmp(program_name, "cat") == 0) return silt_cat_main(argc, argv);
    if (strcmp(program_name, "head") == 0) return silt_head_main(argc, argv);
    if (strcmp(program_name, "tail") == 0) return silt_tail_main(argc, argv);
    if (strcmp(program_name, "wc") == 0) return silt_wc_main(argc, argv);
    if (strcmp(program_name, "pwd") == 0) return silt_pwd_main(argc, argv);
    if (strcmp(program_name, "printenv") == 0) return silt_printenv_main(argc, argv);
    if (strcmp(program_name, "yes") == 0) return silt_yes_main(argc, argv);
    error(0, 0, "available commands: echo basename dirname cat head tail wc pwd printenv yes");
    return 1;
}
