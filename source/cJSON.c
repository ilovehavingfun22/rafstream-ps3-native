#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *skip_ws(const char *in) {
    while (in && *in && (unsigned char)*in <= 32) in++;
    return in;
}

static cJSON *new_item(void) {
    return (cJSON *)calloc(1, sizeof(cJSON));
}

void cJSON_Delete(cJSON *c) {
    cJSON *next;
    while (c) {
        next = c->next;
        if (c->child) cJSON_Delete(c->child);
        if (c->valuestring) free(c->valuestring);
        if (c->string) free(c->string);
        free(c);
        c = next;
    }
}

static const char *parse_value(cJSON *item, const char *value);

static const char *parse_string(cJSON *item, const char *str) {
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;
    if (*str != '\"') return NULL;
    while (*ptr != '\"' && *ptr) {
        if (*ptr++ == '\\') ptr++;
        len++;
    }
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '\"' && *ptr) {
        if (*ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
                case 'b': *ptr2++ = '\b'; break;
                case 'f': *ptr2++ = '\f'; break;
                case 'n': *ptr2++ = '\n'; break;
                case 'r': *ptr2++ = '\r'; break;
                case 't': *ptr2++ = '\t'; break;
                case 'u':
                    *ptr2++ = '?';
                    if (ptr[1] && ptr[2] && ptr[3] && ptr[4]) ptr += 4;
                    break;
                default: *ptr2++ = *ptr; break;
            }
            ptr++;
        }
    }
    *ptr2 = 0;
    if (*ptr == '\"') ptr++;
    item->valuestring = out;
    item->type = cJSON_String;
    return ptr;
}

static const char *parse_number(cJSON *item, const char *num) {
    char *end = NULL;
    double n = strtod(num, &end);
    if (end == num) return NULL;
    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return end;
}

static const char *parse_array(cJSON *item, const char *value) {
    cJSON *child;
    if (*value != '[') return NULL;
    item->type = cJSON_Array;
    value = skip_ws(value + 1);
    if (*value == ']') return value + 1;

    item->child = child = new_item();
    if (!item->child) return NULL;
    value = skip_ws(parse_value(child, skip_ws(value)));
    if (!value) return NULL;

    while (*value == ',') {
        cJSON *new_node = new_item();
        if (!new_node) return NULL;
        child->next = new_node;
        new_node->prev = child;
        child = new_node;
        value = skip_ws(parse_value(child, skip_ws(value + 1)));
        if (!value) return NULL;
    }
    if (*value == ']') return value + 1;
    return NULL;
}

static const char *parse_object(cJSON *item, const char *value) {
    cJSON *child;
    if (*value != '{') return NULL;
    item->type = cJSON_Object;
    value = skip_ws(value + 1);
    if (*value == '}') return value + 1;

    item->child = child = new_item();
    if (!item->child) return NULL;
    value = skip_ws(parse_string(child, skip_ws(value)));
    if (!value) return NULL;
    child->string = child->valuestring;
    child->valuestring = NULL;
    if (*value != ':') return NULL;
    value = skip_ws(parse_value(child, skip_ws(value + 1)));
    if (!value) return NULL;

    while (*value == ',') {
        cJSON *new_node = new_item();
        if (!new_node) return NULL;
        child->next = new_node;
        new_node->prev = child;
        child = new_node;
        value = skip_ws(parse_string(child, skip_ws(value + 1)));
        if (!value) return NULL;
        child->string = child->valuestring;
        child->valuestring = NULL;
        if (*value != ':') return NULL;
        value = skip_ws(parse_value(child, skip_ws(value + 1)));
        if (!value) return NULL;
    }
    if (*value == '}') return value + 1;
    return NULL;
}

static const char *parse_value(cJSON *item, const char *value) {
    if (!value) return NULL;
    if (!strncmp(value, "null", 4))  { item->type = cJSON_NULL;  return value + 4; }
    if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
    if (!strncmp(value, "true", 4))  { item->type = cJSON_True;  item->valueint = 1; return value + 4; }
    if (*value == '\"')              { return parse_string(item, value); }
    if (*value == '-' || (*value >= '0' && *value <= '9')) { return parse_number(item, value); }
    if (*value == '[')               { return parse_array(item, value); }
    if (*value == '{')               { return parse_object(item, value); }
    return NULL;
}

cJSON *cJSON_Parse(const char *value) {
    cJSON *c = new_item();
    if (!c) return NULL;
    if (!parse_value(c, skip_ws(value))) {
        cJSON_Delete(c);
        return NULL;
    }
    return c;
}

int cJSON_GetArraySize(const cJSON *array) {
    cJSON *c = array ? array->child : NULL;
    int i = 0;
    while (c) { i++; c = c->next; }
    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
    cJSON *c = array ? array->child : NULL;
    while (c && index > 0) { index--; c = c->next; }
    return c;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *const object, const char *const string) {
    cJSON *c = object ? object->child : NULL;
    while (c && c->string && strcmp(c->string, string) != 0) c = c->next;
    return c;
}
