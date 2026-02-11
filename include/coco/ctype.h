#ifndef CTYPE_H
#define CTYPE_H

#define isspace(c) ((c)==' ' || ((unsigned char)(c)>=9 && (unsigned char)(c)<=13))
#define isprint(c) (c>=0x20 && c<=0x8E)

#endif /* CTYPE_H */