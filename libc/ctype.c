#include <ctype.h>

int isupper(int character) {
    return character >= 'A' && character <= 'Z';
}

int islower(int character) {
    return character >= 'a' && character <= 'z';
}

int isalpha(int character) {
    return isupper(character) || islower(character);
}

int isdigit(int character) {
    return character >= '0' && character <= '9';
}

int isxdigit(int character) {
    return isdigit(character) || (character >= 'A' && character <= 'F')
        || (character >= 'a' && character <= 'f');
}

int isalnum(int character) {
    return isalpha(character) || isdigit(character);
}

int isspace(int character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

int isblank(int character) {
    return character == ' ' || character == '\t';
}

int iscntrl(int character) {
    return (character >= 0 && character < ' ') || character == 0x7f;
}

int isprint(int character) {
    return character >= ' ' && character <= '~';
}

int isgraph(int character) {
    return character > ' ' && character <= '~';
}

int ispunct(int character) {
    return isgraph(character) && !isalnum(character);
}

int toupper(int character) {
    return islower(character) ? character - 'a' + 'A' : character;
}

int tolower(int character) {
    return isupper(character) ? character - 'A' + 'a' : character;
}
