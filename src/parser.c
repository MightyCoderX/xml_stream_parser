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
#define ERROR(fmt, ...)                                                     \
    fprintf(stderr, "Error on line %zu:%zu (in %s): " fmt, parser.line + 1, \
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
    .otag_stack = NULL,
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

static char* char_to_literal(unsigned char c)
{
    size_t size = 5;
    char* str = malloc(size);

    switch (c)
    {
    case '\n':
        strcpy(str, "\\n");
        break;
    case '\t':
        strcpy(str, "\\t");
        break;
    case '\\':
        strcpy(str, "\\\\");
        break;
    case '"':
        strcpy(str, "\\\"");
        break;
    case '\0':
        strcpy(str, "\\0");
        break;
    default:
        if (isprint(c))
        {
            snprintf(str, size, "%c", c);
        }
        else
        {
            snprintf(str, size, "\\x%02x", (int)c);
        }
        break;
    }
    return str;
}

static void print_repr(char* str, long start, long len)
{
    if (len == -1)
    {
        len = strlen(str) - start;
    }

    putc('"', stderr);
    for (unsigned char* c = (unsigned char*)&str[start];
        c < (unsigned char*)&str[start] + len; c++)
    {
        char* lit = char_to_literal(*c);
        INFO("%s", lit);
        free(lit);
    }
    putc('"', stderr);
    fflush(stderr);
}

static void buf_seek(long amount)
{
    long newidx = parser.bufidx + amount;
    if (newidx < 0 || newidx > (long)sizeof(parser.buf) - 1)
    {
        if (newidx < 0)
        {
            parser.bufidx = 0;
            fseek(parser.file, amount, SEEK_CUR);
        }
        fread(parser.buf, sizeof(parser.buf), 1, parser.file);
        parser.bufidx = 0;
    }
    else
    {
        parser.bufidx += amount;
    }
}

static char next_char()
{
    if (parser.bufidx == sizeof(parser.buf) - 1)
    {
        buf_seek(1);
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

    parser.token[parser.toksize++] = c;
    parser.token[parser.toksize] = '\0';
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

static void free_attrs(Attribute* attrs)
{
    while (attrs != NULL)
    {
        Attribute* next = attrs->next;
        free(attrs);
        attrs = next;
    }
}

static bool is_identifier(char c)
{
    return isalpha(c) || c == '_' || c == '-';
}

static char* trim_left(char* str)
{
    while (isspace(*(str++)))
        ;

    return --str;
}

static char* trim_right(char* str)
{
    size_t len = strlen(str);

    for (long i = len - 1; i >= 0; i--)
    {
        if (!isspace(str[i]))
        {
            str[i + 1] = '\0';
            break;
        }
    }
    return str;
}

static char* trim(char* str)
{
    return trim_right(trim_left(str));
}

void xsp_parser_init(void)
{
    parser.token = calloc(1, parser.tokcap);
}

static void otag_push(char* tagname)
{
    TagNameNode* new = malloc(sizeof(TagNameNode));
    new->tagname = strdup(tagname);

    TagNameNode* head = parser.otag_stack;
    parser.otag_stack = new;
    new->next = head;
}

static char* otag_pop()
{
    char* tagname = parser.otag_stack->tagname;
    TagNameNode* next = parser.otag_stack->next;

    free(parser.otag_stack);
    parser.otag_stack = next;

    return tagname;
}

static void change_state(ParserState next)
{
    INFO("state: %s -> %s\n", state_to_str(parser.state), state_to_str(next));

    parser.state = next;
}

void xsp_parse_file(FILE* file)
{
    parser.file = file;
    fread(parser.buf, sizeof(parser.buf), 1, parser.file);

    Element e = { 0 };
    Attribute* attr = NULL;
    char* text = NULL;
    char* closing_tag = NULL;
    char* opening_tag = NULL;

    char c;
    while ((c = next_char()) != EOF)
    {
        INFO("------ BEFORE SWITCH -------\n");
        char* lit = char_to_literal(c);
        INFO("c: '%s' (%d)\n", lit, c);
        free(lit);
        INFO("buf: ");
        print_repr(parser.buf, parser.bufidx - 1, 20);
        INFO("\n");
        INFO("bufidx: %zu\n", parser.bufidx);
        INFO("token: ");
        print_repr(parser.token, 0, -1);
        INFO("\n");
        INFO("state: %s\n", state_to_str(parser.state));
        INFO("\n");

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
                        change_state(OUT);
                        tok_pop(NULL, 0);
                        on_document_start();
                    }
                }
                else
                {
                    ERROR("invalid xml declaration, ");
                    EXPECT("?", nc);
                    goto cleanup;
                }
            }
            else
            {
                ERROR("invalid xml declaration, ");
                EXPECT("<", c);
                goto cleanup;
            }
            break;
        case ROOT:
            if (c == '<')
            {
                change_state(OTAG);
            }
            else if (isspace(c))
            {
                // ignore
            }
            else
            {
                EXPECT("<", c);
                goto cleanup;
            }
            break;
        case OUT:
            if (c == '<')
            {
                char nc = next_char();
                if (nc == '/')
                {
                    change_state(CTAG);
                }
                else if (is_identifier(nc))
                {
                    tok_addc(nc);
                    change_state(OTAG);
                }
                else
                {
                    EXPECT("[A-Za-z_\\-]", nc);
                    goto cleanup;
                }
            }
            else if (isspace(c))
            {
                // ignore
            }
            else if (c == '\0')
            {
                return;
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
                if (strlen(e.name) == 0)
                {
                    tok_pop(e.name, sizeof(e.name));
                }
                change_state(INTAG);
                otag_push(e.name);
                on_open_tag(e);
                skip_spaces();
                free_attrs(e.attributes);
                e.attributes = NULL;
            }
            else if (is_identifier(c))
            {
                tok_addc(c);
            }
            else if (isspace(c))
            {
                tok_pop(e.name, sizeof(e.name));
                change_state(ATTRNAME);
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
                    change_state(INQUOTES);
                }
                else
                {
                    parser.bufidx--;
                    change_state(ATTRVALUE);
                }
            }
            else if (isspace(c))
            {
                attr = attr_add(&e, NULL, NULL);
                tok_pop(attr->name, sizeof(attr->name));
            }
            else if (is_identifier(c))
            {
                tok_addc(c);
            }
            else if (c == '>')
            {
                buf_seek(-1);
                change_state(OTAG);
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
                change_state(ATTRVALUE);
            }
            break;
        case ATTRVALUE:
            if (c == ' ')
            {
                tok_pop(attr->value, sizeof(attr->value));
                change_state(OTAG);
            }
            else if (c != '>' && c != '\n')
            {
                tok_addc(c);
            }
            else if (c == '>')
            {
                tok_pop(attr->value, sizeof(attr->value));
                buf_seek(-1);
                change_state(OTAG);
            }
            else
            {
                EXPECT(" ' ', '>' or '\n'", c);
            }
            break;
        case INTAG:
            if (c == '<')
            {
                e.name[0] = '\0';
                // text between an opening tag and a opening/closing tag (es. "<p>lol<span>a</span></p>" -> lol)

                text = strdup(trim(parser.token));
                if (strcmp(text, "") != 0)
                {
                    on_text(text);
                }

                free(text);
                text = NULL;

                tok_pop(NULL, 0);

                char nc = next_char();
                if (nc == '/')
                {
                    change_state(CTAG);
                }
                // not is_identifier since an identifier must start with a letter
                else if (isalpha(nc))
                {
                    tok_addc(nc);
                    change_state(OTAG);
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
                closing_tag = malloc(strlen(parser.token) + 1);
                tok_pop(closing_tag, strlen(parser.token));

                opening_tag = otag_pop();

                if (strncmp(closing_tag, opening_tag, strlen(opening_tag)) != 0)
                {
                    ERROR("trying to close tag '%s' with '%s'\n", opening_tag,
                        closing_tag);
                    goto cleanup;
                }

                on_close_tag(closing_tag);

                free(closing_tag);
                closing_tag = NULL;
                free(opening_tag);
                opening_tag = NULL;

                free_attrs(e.attributes);
                e.attributes = NULL;

                e = (Element) { 0 };

                change_state(OUT);
            }
            else
            {
                EXPECT("[A-Za-z]+ or >", c);
                return;
            }
            break;
        }
    }

cleanup:
    free(text);
    free(closing_tag);
    free(opening_tag);
    free_attrs(e.attributes);
}
