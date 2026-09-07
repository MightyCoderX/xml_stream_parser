#include <stdio.h>
#include <stdlib.h>

#include "parser.h"

void on_document_start(void)
{
    printf("document started\n");
}

void on_open_tag(Element element)
{
    printf("element opened: %s\n", element.name);
}

void on_close_tag(Element element)
{
    printf("element closed: %s\n", element.name);
}

void on_text(char* string)
{
    printf("text: %s\n", string);
    free(string);
}

int main(int argc, char** argv)
{
    FILE* file = fopen(argv[1], "r");
    if (!file)
    {
        perror("fopen");
        return 1;
    }

    xsp_parser_init();
    xsp_parse_file(file);
    return 0;
}
