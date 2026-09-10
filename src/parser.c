#include <assert.h>
#include <bits/pthreadtypes.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

#define INFO(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)                                                \
    fprintf(stderr, "line %zu:%zu %s: " fmt, parser.line + 1,          \
        parser.column + 1, state_to_str(parser.state), ##__VA_ARGS__);

#define EXPECT(exp, got) ERROR("expected '%s' got '%c' (%d)\n", exp, got, got)

static Parser parser = {
    .state = DECL,
    .file = NULL,
    .bufidx = 0,
    .toksize = 0,
    .token = NULL,
    .tokcap = 128,
    .line = 0,
    .column = 0,
};

static char* state_to_str(ParserState state)
{
    switch (state)
    {
    case DECL:
        return "DECL";
    case OUT:
        return "OUT";
    case ROOT:
        return "ROOT";
    case OTAG:
        return "OTAG";
    case ATTRNAME:
        return "ATTRNAME";
    case ATTRVALUE:
        return "ATTRVALUE";
    case INQUOTES:
        return "INQUOTES";
    case CTAG:
        return "CTAG";
    case INTAG:
        return "INTAG";
    }

    return "UNKNOWN";
}

static void next_chunk()
{
    fread(parser.buf, sizeof(parser.buf), 1, parser.file);
    parser.bufidx = 0;
}

static char next_char()
{
    if (parser.bufidx == sizeof(parser.buf) - 1)
    {
        next_chunk();
    }

    char c = parser.buf[parser.bufidx++];

    parser.column++;
    if (c == '\n')
    {
        parser.line++;
        parser.column = 0;
    }

    return c;
}

static void skip_spaces()
{
    while (isspace(next_char()))
        ;
    parser.bufidx--;
}

static void tok_addc(char c)
{
    if (parser.toksize == parser.tokcap)
    {
        parser.tokcap *= 1.5;
        char* newtoken = realloc(parser.token, parser.tokcap);
        assert(newtoken != NULL && "Buy more RAM");

        parser.token = newtoken;
    }

    INFO("tok_addc: %c (%d)\n", c, c);

    parser.token[parser.toksize++] = c;
}

static void tok_pop(char* out, size_t size)
{
    if (out != NULL)
    {
        size_t len = parser.toksize;
        len = len <= size ? len : size;

        strncpy(out, parser.token, len);
        out[len] = '\0';
    }
    parser.toksize = 0;
    parser.token[0] = '\0';
}

static Attribute* attr_add(Element* e, char* name, char* value)
{
    Attribute* attr = calloc(1, sizeof(*attr));

    if (name != NULL)
    {
        strncpy(attr->name, name, sizeof(attr->name));
        attr->name[strlen((attr->name)) - 1] = '\0';
    }

    if (value != NULL)
    {
        strncpy(attr->value, value, sizeof(attr->value));
        attr->value[sizeof(attr->value) - 1] = '\0';
    }

    attr->next = e->attributes;
    e->attributes = attr;

    return attr;
}

static bool is_identifier(char c)
{
    return isalpha(c) || c == '_' || c == '-';
}

void xsp_parser_init(void)
{
    parser.token = malloc(parser.tokcap);
}

void xsp_parse_file(FILE* file)
{
    parser.file = file;
    next_chunk();

    ParserState prev = DECL;

    Element e = { 0 };
    Attribute* attr = NULL;
    char c;
    while ((c = next_char()) != EOF)
    {
        INFO("c: '%c' (%d)\n", c, c);
        INFO("buf: %.20s\n", &parser.buf[parser.bufidx]);
        INFO("bufidx: %zu\n", parser.bufidx);
        INFO("token: %.*s\n", (int)parser.toksize, parser.token);
        INFO("state: %s\n", state_to_str(parser.state));
        switch (parser.state)
        {
        case DECL:
            if (c == '<')
            {
                char nc = next_char();
                if (nc == '?')
                {
                    while ((nc = next_char()) != '>')
                    {
                        tok_addc(nc);
                    }

                    parser.token[parser.toksize] = '\0';

                    if (strcmp(parser.token,
                            "xml version=\"1.0\" encoding=\"UTF-8\"?") == 0)
                    {
                        parser.state = OUT;
                        tok_pop(NULL, 0);
                        on_document_start();
                    }
                }
                else
                {
                    ERROR("invalid xml declaration, ");
                    EXPECT("?", nc);
                    return;
                }
            }
            else
            {
                ERROR("invalid xml declaration, ");
                EXPECT("<", c);
                return;
            }
            break;
        case ROOT:
            if (c == '<')
            {
                parser.state = OTAG;
            }
            else if (c == '\n' || c == ' ')
            {
                // ignore
            }
            else
            {
                EXPECT("<", c);
                return;
            }
            break;
        case OUT:
            if (c == '<')
            {
                parser.state = OTAG;
            }
            else if (c == '\n' || c == ' ')
            {
                // ignore
            }
            else
            {
                EXPECT("opening tag", c);
                return;
            }
            break;
        case OTAG:
            if (c == '>')
            {
                tok_pop(e.name, sizeof(e.name));
                on_open_tag(e);
                parser.state = INTAG;
            }
            else if (is_identifier(c))
            {
                tok_addc(c);
            }
            else if (c == ' ')
            {
                parser.state = ATTRNAME;
                skip_spaces();
            }
            else
            {
                EXPECT("> or [A-Za-z]+", c);
                return;
            }
            break;
        case ATTRNAME:
            if (c == '=')
            {
                attr = attr_add(&e, NULL, NULL);
                tok_pop(attr->name, sizeof(attr->name));

                char nc = next_char();
                if (nc == '"')
                {
                    prev = parser.state;
                    parser.state = INQUOTES;
                }
                else
                {
                    parser.bufidx--;
                    parser.state = ATTRVALUE;
                }
            }
            else if (isspace(c))
            {
                // ignore
            }
            else if (c != '>')
            {
                tok_addc(c);
            }
            else
            {
                ERROR("unexpected tag ending after '='\n");
                return;
            }
            break;
        case INQUOTES:
            if (c != '"')
            {
                tok_addc(c);
            }
            else
            {
                parser.state = prev;
            }
            break;
        case ATTRVALUE:
            if (c == ' ')
            {
                tok_pop(attr->value, sizeof(attr->value));
            }
            else if (c != '>' && c != '\n')
            {
                tok_addc(c);
            }
            else
            {
                parser.state = INTAG;
            }
            break;
        case INTAG:
            if (c == '<')
            {
                char* text = malloc(parser.toksize + 1);
                tok_pop(text, parser.toksize);
                on_text(text);

                char nc = next_char();
                if (nc == '/')
                {
                    parser.state = CTAG;
                    e = (Element) { 0 };
                }
                else if (is_identifier(nc))
                {
                    parser.state = OTAG;
                    tok_addc(nc);
                }
                else
                {
                    EXPECT("closing or opening tag", nc);
                    return;
                }
            }
            else
            {
                tok_addc(c);
            }
            break;
        case CTAG:
            if (is_identifier(c))
            {
                tok_addc(c);
            }
            else if (c == '>')
            {
                parser.state = OUT;
            }
            else
            {
                EXPECT("[A-Za-z]+ or >", c);
                return;
            }
            break;
        }
    }
}
