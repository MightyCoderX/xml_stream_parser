#ifndef PARSER_H_
#define PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct Attribute Attribute;
typedef struct Element Element;

typedef enum {
    DECL,
    OUT,
    ROOT,
    OTAG,
    ATTRNAME,
    ATTRVALUE,
    INQUOTES,
    CTAG,
    INTAG,
} ParserState;

typedef struct TagNameNode TagNameNode;

struct TagNameNode {
    char* tagname;
    TagNameNode* next;
};

typedef struct {
    ParserState state;
    FILE* file;
    size_t bufidx;
    size_t toksize;
    size_t tokcap;
    char buf[4096];
    char* token;
    size_t line;
    size_t column;
    TagNameNode* otag_stack;
} Parser;

struct Attribute {
    char name[32];
    char value[128];
    Attribute* next;
};

struct Element {
    char name[64];
    Attribute* attributes;
};

void xsp_parser_init(void);
void xsp_parse_file(FILE* file);

void on_document_start(void);
void on_open_tag(Element element);
void on_close_tag(char* tag_name);
void on_text(char* string);

#endif
