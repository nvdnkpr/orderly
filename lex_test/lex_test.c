#include "../src/orderly_lex.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAX_INPUT_TEXT (1 << 20)

static const char * tokToStr(orderly_tok t)
{
    switch (t) {
        case orderly_tok_error: return "error";
        case orderly_tok_eof: return "eof";
        case orderly_tok_semicolon:     return "semicolon";
        case orderly_tok_left_bracket: return "l_bracket";
        case orderly_tok_left_curly: return "l_curly";
        case orderly_tok_right_bracket: return "r_bracket";
        case orderly_tok_right_curly: return "r_curly";
        case orderly_tok_lt:     return "lt";
        case orderly_tok_gt: return "gt";
    }
    return "unknown";
}


int
main(int argc, char ** argv) 
{
    /* this is not a stream lexer, let's read as much as we can up to a
     * reasonable limit  (1 meg) */
    static char inbuf[MAX_INPUT_TEXT];
    size_t tot = 0, rd;
    while (0 < (rd = read(0, (void *) (inbuf + tot), MAX_INPUT_TEXT)))
        tot += rd;

    {
        orderly_tok t;
        unsigned int off = 0;
        orderly_lexer lexer = orderly_lex_alloc(NULL);
        const unsigned char * outBuf = NULL;
        unsigned int outLen = 0;

        do {
            t = orderly_lex_lex(lexer, (const unsigned char *) inbuf,
                                tot, &off, &outBuf, &outLen);
            printf("(%3u,%3u): '",
                   orderly_lex_current_line(lexer),
                   orderly_lex_current_char(lexer));
            fwrite(outBuf, sizeof(char), outLen, stdout);
            printf("' %s\n", tokToStr(t));
            
        } while (t != orderly_tok_error && t != orderly_tok_eof);
    }
    
    return 0;
}