#include <stdio.h>

#include "parser.h"

void on_document_start(void)
{
    printf("document started\n");
    fflush(stdout);
}

void on_open_tag(Element element)
{
    printf("\nelement opened: %s\n", element.name);
    while (element.attributes != NULL)
    {
        printf("  %s = %s\n", element.attributes->name,
            element.attributes->value);
        element.attributes = element.attributes->next;
    }
    fflush(stdout);
}

void on_close_tag(Element element)
{
    printf("element closed: %s\n", element.name);
    fflush(stdout);
}

void on_text(char* string)
{
    printf("text: \"%s\"\n", string);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        return 1;
    }

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
