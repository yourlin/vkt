/*
 * SPDX-FileCopyrightText: 2026 yourlin
 * SPDX-License-Identifier: MIT-0
 *
 * A small YAML reader for vkt's configuration files: the subset they use.
 *
 *   - block mappings (key: value, key: followed by an indented block);
 *   - block sequences (- item) of scalars, and flow sequences [a, b];
 *   - scalars: plain, 'single' or "double" quoted (no escapes beyond \" and
 *     \\ in double quotes);
 *   - comments (# at the start or after a blank), blank lines.
 *
 * Anything else (anchors, tags, multi-line scalars, flow mappings, tabs for
 * indentation) is an error with its line number.
 */

#ifndef YAML_H
#define YAML_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ynode_type { Y_NULL, Y_SCALAR, Y_MAP, Y_SEQ };

struct ynode {
   enum ynode_type type;
   int line;
   char *str;           /* Y_SCALAR */
   char **keys;         /* Y_MAP: keys[i] -> kids[i] */
   struct ynode **kids; /* Y_MAP values, Y_SEQ items */
   int n;
};

struct yline {
   int no; /* 1-based */
   int indent;
   char *text; /* without indentation, comment and trailing blanks */
};

struct yparser {
   const char *path;
   struct yline *lines;
   int n, pos;
   char err[256];
};

static struct ynode *
ynew(enum ynode_type t, int line)
{
   struct ynode *n = calloc(1, sizeof(*n));
   n->type = t;
   n->line = line;
   return n;
}

static void
yadd(struct ynode *parent, char *key, struct ynode *kid)
{
   parent->kids = realloc(parent->kids, (parent->n + 1) * sizeof(*parent->kids));
   parent->kids[parent->n] = kid;
   if (parent->type == Y_MAP) {
      parent->keys = realloc(parent->keys, (parent->n + 1) * sizeof(*parent->keys));
      parent->keys[parent->n] = key;
   }
   parent->n++;
}

static int
yfail(struct yparser *p, int line, const char *msg)
{
   if (!p->err[0])
      snprintf(p->err, sizeof(p->err), "%s:%d: %s", p->path, line, msg);
   return 0;
}

/* Removes a comment (# at the start or after a blank, outside quotes) and
 * trailing blanks. */
static void
ystrip(char *s)
{
   char quote = 0;
   for (char *c = s; *c; c++) {
      if (quote) {
         if (*c == '\\' && quote == '"' && c[1])
            c++;
         else if (*c == quote)
            quote = 0;
      } else if (*c == '\'' || *c == '"') {
         quote = *c;
      } else if (*c == '#' && (c == s || c[-1] == ' ')) {
         *c = 0;
         break;
      }
   }
   size_t l = strlen(s);
   while (l && (s[l - 1] == ' ' || s[l - 1] == '\r' || s[l - 1] == '\n'))
      s[--l] = 0;
}

/* A scalar: quoted or plain. NULL (and an error) if malformed. */
static char *
yscalar(struct yparser *p, int line, const char *s)
{
   if (*s == '\'' || *s == '"') {
      const char q = *s++;
      char *out = malloc(strlen(s) + 1), *o = out;
      for (; *s && *s != q; s++) {
         if (q == '"' && *s == '\\' && (s[1] == '"' || s[1] == '\\'))
            s++;
         *o++ = *s;
      }
      *o = 0;
      if (*s != q || s[1]) {
         free(out);
         yfail(p, line, "unterminated or trailing text after a quoted string");
         return NULL;
      }
      return out;
   }
   if (*s == '&' || *s == '*' || *s == '!' || *s == '{' || *s == '|' || *s == '>') {
      yfail(p, line, "unsupported YAML (anchor, alias, tag, flow mapping or block scalar)");
      return NULL;
   }
   return strdup(s);
}

/* A value on the same line as its key or dash: [a, b], a scalar, or
 * nothing (Y_NULL). */
static struct ynode *
yinline(struct yparser *p, int line, const char *s)
{
   if (!*s)
      return ynew(Y_NULL, line);
   if (*s != '[') {
      char *v = yscalar(p, line, s);
      if (!v)
         return NULL;
      struct ynode *n = ynew(Y_SCALAR, line);
      n->str = v;
      return n;
   }
   struct ynode *seq = ynew(Y_SEQ, line);
   const char *end = strrchr(s, ']');
   if (!end || end[1]) {
      yfail(p, line, "a flow sequence must end with ] on the same line");
      return NULL;
   }
   char *body = strndup(s + 1, end - s - 1);
   for (char *item = strtok(body, ","); item; item = strtok(NULL, ",")) {
      while (*item == ' ')
         item++;
      size_t l = strlen(item);
      while (l && item[l - 1] == ' ')
         item[--l] = 0;
      if (!*item)
         continue;
      struct ynode *kid = yinline(p, line, item);
      if (!kid || kid->type != Y_SCALAR) {
         free(body);
         yfail(p, line, "flow sequence items must be scalars");
         return NULL;
      }
      yadd(seq, NULL, kid);
   }
   free(body);
   return seq;
}

static struct ynode *yblock(struct yparser *p, int indent);

/* The value of a key or dash: on its line, or the indented block after. */
static struct ynode *
yvalue(struct yparser *p, int line, int indent, const char *rest)
{
   if (*rest)
      return yinline(p, line, rest);
   if (p->pos < p->n && p->lines[p->pos].indent > indent)
      return yblock(p, p->lines[p->pos].indent);
   /* A sequence may sit at its key's indentation (key:\n- a). */
   if (p->pos < p->n && p->lines[p->pos].indent == indent && p->lines[p->pos].text[0] == '-')
      return yblock(p, indent);
   return ynew(Y_NULL, line);
}

/* The lines at indentation indent, from p->pos: a mapping or a sequence. */
static struct ynode *
yblock(struct yparser *p, int indent)
{
   const struct yline *first = &p->lines[p->pos];
   const int is_seq = first->text[0] == '-' && (first->text[1] == ' ' || !first->text[1]);
   struct ynode *node = ynew(is_seq ? Y_SEQ : Y_MAP, first->no);
   while (p->pos < p->n && p->lines[p->pos].indent >= indent) {
      const struct yline *l = &p->lines[p->pos];
      if (l->indent > indent) {
         yfail(p, l->no, "unexpected indentation");
         return NULL;
      }
      if (is_seq) {
         if (l->text[0] != '-' || (l->text[1] && l->text[1] != ' ')) {
            yfail(p, l->no, "expected '- item' in a sequence");
            return NULL;
         }
         p->pos++;
         const char *rest = l->text + 1;
         while (*rest == ' ')
            rest++;
         if (strchr(rest, ':') && rest[0] != '\'' && rest[0] != '"' && rest[0] != '[') {
            const char *c = strchr(rest, ':');
            if (c[1] == ' ' || !c[1]) {
               yfail(p, l->no, "mappings inside sequences are not supported");
               return NULL;
            }
         }
         struct ynode *kid = yvalue(p, l->no, indent, rest);
         if (!kid)
            return NULL;
         yadd(node, NULL, kid);
      } else {
         if (l->text[0] == '-') {
            yfail(p, l->no, "a sequence item where a key was expected");
            return NULL;
         }
         char *colon = l->text;
         while ((colon = strchr(colon, ':')) && colon[1] && colon[1] != ' ')
            colon++;
         if (!colon) {
            yfail(p, l->no, "expected 'key: value'");
            return NULL;
         }
         char *key = strndup(l->text, colon - l->text);
         const char *rest = colon + 1;
         while (*rest == ' ')
            rest++;
         for (int i = 0; i < node->n; i++) {
            if (!strcmp(node->keys[i], key)) {
               yfail(p, l->no, "duplicate key");
               return NULL;
            }
         }
         p->pos++;
         struct ynode *kid = yvalue(p, l->no, indent, rest);
         if (!kid)
            return NULL;
         yadd(node, key, kid);
      }
   }
   return node;
}

/* The document in path, or NULL with the error in err. An empty file is a
 * Y_NULL node. */
static struct ynode *
yload(const char *path, char *err, size_t err_size)
{
   struct yparser p = {.path = path};
   FILE *f = fopen(path, "r");
   if (!f) {
      snprintf(err, err_size, "cannot open %s", path);
      return NULL;
   }
   char buf[1024];
   int no = 0;
   while (fgets(buf, sizeof(buf), f)) {
      no++;
      if (no == 1 && !strncmp(buf, "---", 3) && (buf[3] == '\n' || !buf[3]))
         continue;
      int indent = 0;
      while (buf[indent] == ' ')
         indent++;
      if (buf[indent] == '\t') {
         snprintf(err, err_size, "%s:%d: tabs are not allowed for indentation", path, no);
         fclose(f);
         return NULL;
      }
      ystrip(buf + indent);
      if (!buf[indent])
         continue;
      p.lines = realloc(p.lines, (p.n + 1) * sizeof(*p.lines));
      p.lines[p.n++] = (struct yline){no, indent, strdup(buf + indent)};
   }
   fclose(f);
   if (!p.n)
      return ynew(Y_NULL, 1);
   struct ynode *root = yblock(&p, p.lines[0].indent);
   if (root && p.pos < p.n)
      yfail(&p, p.lines[p.pos].no, "unexpected indentation");
   if (p.err[0]) {
      snprintf(err, err_size, "%s", p.err);
      return NULL;
   }
   return root;
}

#endif
