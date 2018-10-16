/* BSD 2-Clause License
 *
 * Copyright (c) 2018, Wojciech Owczarek
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * @file   barser_defaults.h
 * @date   Sun Apr15 13:40:12 2018
 *
 * @brief  default barser token types, meanings and other default behaviour
 *
 */

#ifndef BARSER_DEFAULTS_H_
#define BARSER_DEFAULTS_H_

/* control characters */


/* BS_ENDVAL_CHAR must be defined - this is the default value separator */
#define BS_ENDVAL_CHAR          ';'	/* end of value / value separator, Juniper / gated style */
/* BS_ENDVAL1_CHAR up to BS_ENDVAL5_CHAR may optionally be defined */
#define BS_ENDVAL1_CHAR         ','	/* end of value / value separator, JSON style */

/* BS_QUOTE_CHAR must be defined */
#define BS_QUOTE_CHAR        '"'	/* double quote character */
/* BS_QUOTE1_CHAR up to BS_QUOTE3_CHAR may be defined */
#define BS_QUOTE1_CHAR        '\''	/* single quote character */

#define BS_STARTBLOCK_CHAR      '{'	/* start of block */
#define BS_ENDBLOCK_CHAR        '}'	/* end of block */
#define BS_ESCAPE_CHAR          '\\'	/* escape character */
#define BS_COMMENT_CHAR         '#'	/* comment marker */
#define BS_MLCOMMENT_OUT_CHAR   '/'	/* multi-line comment outer character */
#define BS_MLCOMMENT_IN_CHAR    '*'	/* multi-line comment inner character */
#define BS_STARTARRAY_CHAR      '['	/* start of array */
#define BS_ENDARRAY_CHAR        ']'	/* end of array */
#define BS_ARRAYSEP_CHAR        ','	/* optional, really used for output only */
#define BS_INDENT_CHAR          ' '	/* indentation */

#define BS_PATH_SEP		'/'	/* path separator for queries */

/* maximum line width displayed when showing an error */
#define BS_ERRORDUMP_LINEWIDTH 80

/* indent size - if space is chosen, can be say 4 or 8 */
#define BS_INDENT_WIDTH 4

/* initial allocation size for a quoted string */
#define BS_QUOTED_STARTSIZE 50

/* maximum number of consecutive tokens when declaring a value - we have to stop somewhere... */
#define BS_MAX_TOKENS 20

/* character class flags */
#define BF_NON		0	/* no flags */
#define BF_TOK		(1<<0)	/* legal part of a token */
#define BF_EXT		(1<<1)	/* extended token characters (used in second and further tokens) */
#define BF_CTL		(1<<2)	/* control characters */
#define BF_SPC		(1<<3)	/* whitespace characters */
#define BF_NLN		(1<<4)	/* newline characters */
#define BF_ILL		(1<<5)	/* illegal characters */
#define BF_ESC		(1<<6)	/* escapable characters */
#define BF_ESS		(1<<7)	/* escape sequences */

/*
 * Static character to class mappings. a character can belong to multiple classes,
 * so that it can be treated differently depending on scanner state machine state.
 * Example: ':' is both BF_SPC and BF_EXT, so when grabbing a token, ':' can be part
 * of a token. All flags are ORed with BF_NON so extra flags can be added easily.
 * ...And yes, "do not put static declarations into header files" - but this header
 * is only meant to be included by barser.h, so, you know.
 */

static const char chflags[256] = {

    [  0] = BF_CTL | BF_NON /* NUL */, [ 64] = BF_TOK | BF_NON /* @   */, [128] = BF_ILL | BF_NON, [192] = BF_ILL | BF_NON,
    [  1] = BF_ILL | BF_NON /* SOH */, [ 65] = BF_TOK | BF_NON /* A   */, [129] = BF_ILL | BF_NON, [193] = BF_ILL | BF_NON,
    [  2] = BF_ILL | BF_NON /* STX */, [ 66] = BF_TOK | BF_NON /* B   */, [130] = BF_ILL | BF_NON, [194] = BF_ILL | BF_NON,
    [  3] = BF_ILL | BF_NON /* ETX */, [ 67] = BF_TOK | BF_NON /* C   */, [131] = BF_ILL | BF_NON, [195] = BF_ILL | BF_NON,
    [  4] = BF_ILL | BF_NON /* EOT */, [ 68] = BF_TOK | BF_NON /* D   */, [132] = BF_ILL | BF_NON, [196] = BF_ILL | BF_NON,
    [  5] = BF_ILL | BF_NON /* ENQ */, [ 69] = BF_TOK | BF_NON /* E   */, [133] = BF_ILL | BF_NON, [197] = BF_ILL | BF_NON,
    [  6] = BF_ILL | BF_NON /* ACK */, [ 70] = BF_TOK | BF_NON /* F   */, [134] = BF_ILL | BF_NON, [198] = BF_ILL | BF_NON,
    [  7] = BF_ILL | BF_NON /* BEL */, [ 71] = BF_TOK | BF_NON /* G   */, [135] = BF_ILL | BF_NON, [199] = BF_ILL | BF_NON,
    [  8] = BF_ILL | BF_ESC /* BS  */, [ 72] = BF_TOK | BF_NON /* H   */, [136] = BF_ILL | BF_NON, [200] = BF_ILL | BF_NON,
    [  9] = BF_SPC | BF_ESC /* TAB */, [ 73] = BF_TOK | BF_NON /* I   */, [137] = BF_ILL | BF_NON, [201] = BF_ILL | BF_NON,
    [ 10] = BF_NLN | BF_ESC /* LF  */, [ 74] = BF_TOK | BF_NON /* J   */, [138] = BF_ILL | BF_NON, [202] = BF_ILL | BF_NON,
    [ 11] = BF_ILL | BF_NON /* VT  */, [ 75] = BF_TOK | BF_NON /* K   */, [139] = BF_ILL | BF_NON, [203] = BF_ILL | BF_NON,
    [ 12] = BF_ILL | BF_ESC /* FF  */, [ 76] = BF_TOK | BF_NON /* L   */, [140] = BF_ILL | BF_NON, [204] = BF_ILL | BF_NON,
    [ 13] = BF_NLN | BF_ESC /* CR  */, [ 77] = BF_TOK | BF_NON /* M   */, [141] = BF_ILL | BF_NON, [205] = BF_ILL | BF_NON,
    [ 14] = BF_ILL | BF_NON /* SO  */, [ 78] = BF_TOK | BF_NON /* N   */, [142] = BF_ILL | BF_NON, [206] = BF_ILL | BF_NON,
    [ 15] = BF_ILL | BF_NON /* SI  */, [ 79] = BF_TOK | BF_NON /* O   */, [143] = BF_ILL | BF_NON, [207] = BF_ILL | BF_NON,
    [ 16] = BF_ILL | BF_NON /* DLE */, [ 80] = BF_TOK | BF_NON /* P   */, [144] = BF_ILL | BF_NON, [208] = BF_ILL | BF_NON,
    [ 17] = BF_ILL | BF_NON /* DC1 */, [ 81] = BF_TOK | BF_NON /* Q   */, [145] = BF_ILL | BF_NON, [209] = BF_ILL | BF_NON,
    [ 18] = BF_ILL | BF_NON /* DC2 */, [ 82] = BF_TOK | BF_NON /* R   */, [146] = BF_ILL | BF_NON, [210] = BF_ILL | BF_NON,
    [ 19] = BF_ILL | BF_NON /* DC3 */, [ 83] = BF_TOK | BF_NON /* S   */, [147] = BF_ILL | BF_NON, [211] = BF_ILL | BF_NON,
    [ 20] = BF_ILL | BF_NON /* DC4 */, [ 84] = BF_TOK | BF_NON /* T   */, [148] = BF_ILL | BF_NON, [212] = BF_ILL | BF_NON,
    [ 21] = BF_ILL | BF_NON /* NAK */, [ 85] = BF_TOK | BF_NON /* U   */, [149] = BF_ILL | BF_NON, [213] = BF_ILL | BF_NON,
    [ 22] = BF_ILL | BF_NON /* SYN */, [ 86] = BF_TOK | BF_NON /* V   */, [150] = BF_ILL | BF_NON, [214] = BF_ILL | BF_NON,
    [ 23] = BF_ILL | BF_NON /* ETB */, [ 87] = BF_TOK | BF_NON /* W   */, [151] = BF_ILL | BF_NON, [215] = BF_ILL | BF_NON,
    [ 24] = BF_ILL | BF_NON /* CAN */, [ 88] = BF_TOK | BF_NON /* X   */, [152] = BF_ILL | BF_NON, [216] = BF_ILL | BF_NON,
    [ 25] = BF_ILL | BF_NON /* EM  */, [ 89] = BF_TOK | BF_NON /* Y   */, [153] = BF_ILL | BF_NON, [217] = BF_ILL | BF_NON,
    [ 26] = BF_ILL | BF_NON /* SUB */, [ 90] = BF_TOK | BF_NON /* Z   */, [154] = BF_ILL | BF_NON, [218] = BF_ILL | BF_NON,
    [ 27] = BF_ILL | BF_NON /* ESC */, [ 91] =BF_CTL|BF_ESC|BF_ESS/*[ */, [155] = BF_ILL | BF_NON, [219] = BF_ILL | BF_NON,
    [ 28] = BF_ILL | BF_NON /* FS  */, [ 92] = BF_ESS | BF_ESC /* \   */, [156] = BF_ILL | BF_NON, [220] = BF_ILL | BF_NON,
    [ 29] = BF_ILL | BF_NON /* GS  */, [ 93] =BF_CTL|BF_ESC|BF_ESS/*] */, [157] = BF_ILL | BF_NON, [221] = BF_ILL | BF_NON,
    [ 30] = BF_ILL | BF_NON /* RS  */, [ 94] = BF_TOK | BF_NON /* ^   */, [158] = BF_ILL | BF_NON, [222] = BF_ILL | BF_NON,
    [ 31] = BF_ILL | BF_NON /* US  */, [ 95] = BF_TOK | BF_NON /* _   */, [159] = BF_ILL | BF_NON, [223] = BF_ILL | BF_NON,
    [ 32] = BF_SPC | BF_NON /* SPC */, [ 96] = BF_ILL | BF_NON /* `   */, [160] = BF_ILL | BF_NON, [224] = BF_ILL | BF_NON,
    [ 33] = BF_ILL | BF_NON /* !   */, [ 97] = BF_TOK | BF_NON /* a   */, [161] = BF_ILL | BF_NON, [225] = BF_ILL | BF_NON,
    [ 34] = BF_CTL|BF_ESC|BF_ESS/* */, [ 98] = BF_TOK | BF_ESS /* b   */, [162] = BF_ILL | BF_NON, [226] = BF_ILL | BF_NON,
    [ 35] = BF_CTL | BF_NON /* #   */, [ 99] = BF_TOK | BF_NON /* c   */, [163] = BF_ILL | BF_NON, [227] = BF_ILL | BF_NON,
    [ 36] = BF_ILL | BF_NON /* $   */, [100] = BF_TOK | BF_NON /* d   */, [164] = BF_ILL | BF_NON, [228] = BF_ILL | BF_NON,
    [ 37] = BF_ILL | BF_NON /* %   */, [101] = BF_TOK | BF_NON /* e   */, [165] = BF_ILL | BF_NON, [229] = BF_ILL | BF_NON,
    [ 38] = BF_ILL | BF_NON /* &   */, [102] = BF_TOK | BF_ESS /* f   */, [166] = BF_ILL | BF_NON, [230] = BF_ILL | BF_NON,
    [ 39] = BF_CTL|BF_ESC|BF_ESS/*'*/, [103] = BF_TOK | BF_NON /* g   */, [167] = BF_ILL | BF_NON, [231] = BF_ILL | BF_NON,
    [ 40] = BF_ILL | BF_NON /* (   */, [104] = BF_TOK | BF_NON /* h   */, [168] = BF_ILL | BF_NON, [232] = BF_ILL | BF_NON,
    [ 41] = BF_ILL | BF_NON /* )   */, [105] = BF_TOK | BF_NON /* i   */, [169] = BF_ILL | BF_NON, [233] = BF_ILL | BF_NON,
    [ 42] = BF_TOK | BF_NON /* *   */, [106] = BF_TOK | BF_NON /* j   */, [170] = BF_ILL | BF_NON, [234] = BF_ILL | BF_NON,
    [ 43] = BF_TOK | BF_NON /* +   */, [107] = BF_TOK | BF_NON /* k   */, [171] = BF_ILL | BF_NON, [235] = BF_ILL | BF_NON,
    [ 44] = BF_CTL | BF_NON /* ,   */, [108] = BF_TOK | BF_NON /* l   */, [172] = BF_ILL | BF_NON, [236] = BF_ILL | BF_NON,
    [ 45] = BF_TOK | BF_NON /* -   */, [109] = BF_TOK | BF_NON /* m   */, [173] = BF_ILL | BF_NON, [237] = BF_ILL | BF_NON,
    [ 46] = BF_TOK | BF_NON /* .   */, [110] = BF_TOK | BF_ESS /* n   */, [174] = BF_ILL | BF_NON, [238] = BF_ILL | BF_NON,
    [ 47] = BF_TOK | BF_NON /* /   */, [111] = BF_TOK | BF_NON /* o   */, [175] = BF_ILL | BF_NON, [239] = BF_ILL | BF_NON,
    [ 48] = BF_TOK | BF_NON /* 0   */, [112] = BF_TOK | BF_NON /* p   */, [176] = BF_ILL | BF_NON, [240] = BF_ILL | BF_NON,
    [ 49] = BF_TOK | BF_NON /* 1   */, [113] = BF_TOK | BF_NON /* q   */, [177] = BF_ILL | BF_NON, [241] = BF_ILL | BF_NON,
    [ 50] = BF_TOK | BF_NON /* 2   */, [114] = BF_TOK | BF_ESS /* r   */, [178] = BF_ILL | BF_NON, [242] = BF_ILL | BF_NON,
    [ 51] = BF_TOK | BF_NON /* 3   */, [115] = BF_TOK | BF_NON /* s   */, [179] = BF_ILL | BF_NON, [243] = BF_ILL | BF_NON,
    [ 52] = BF_TOK | BF_NON /* 4   */, [116] = BF_TOK | BF_ESS /* t   */, [180] = BF_ILL | BF_NON, [244] = BF_ILL | BF_NON,
    [ 53] = BF_TOK | BF_NON /* 5   */, [117] = BF_TOK | BF_NON /* u   */, [181] = BF_ILL | BF_NON, [245] = BF_ILL | BF_NON,
    [ 54] = BF_TOK | BF_NON /* 6   */, [118] = BF_TOK | BF_NON /* v   */, [182] = BF_ILL | BF_NON, [246] = BF_ILL | BF_NON,
    [ 55] = BF_TOK | BF_NON /* 7   */, [119] = BF_TOK | BF_NON /* w   */, [183] = BF_ILL | BF_NON, [247] = BF_ILL | BF_NON,
    [ 56] = BF_TOK | BF_NON /* 8   */, [120] = BF_TOK | BF_NON /* x   */, [184] = BF_ILL | BF_NON, [248] = BF_ILL | BF_NON,
    [ 57] = BF_TOK | BF_NON /* 9   */, [121] = BF_TOK | BF_NON /* y   */, [185] = BF_ILL | BF_NON, [249] = BF_ILL | BF_NON,
    [ 58] = BF_SPC | BF_EXT /* :   */, [122] = BF_TOK | BF_NON /* z   */, [186] = BF_ILL | BF_NON, [250] = BF_ILL | BF_NON,
    [ 59] = BF_CTL | BF_NON /* ;   */, [123] = BF_CTL | BF_NON /* {   */, [187] = BF_ILL | BF_NON, [251] = BF_ILL | BF_NON,
    [ 60] = BF_TOK | BF_NON /* <   */, [124] = BF_SPC | BF_NON /* |   */, [188] = BF_ILL | BF_NON, [252] = BF_ILL | BF_NON,
    [ 61] = BF_SPC | BF_NON /* =   */, [125] = BF_CTL | BF_NON /* }   */, [189] = BF_ILL | BF_NON, [253] = BF_ILL | BF_NON,
    [ 62] = BF_TOK | BF_NON /* >   */, [126] = BF_TOK | BF_NON /* ~   */, [190] = BF_ILL | BF_NON, [254] = BF_ILL | BF_NON,
    [ 63] = BF_TOK | BF_NON /* ?   */, [127] = BF_ILL | BF_NON /* DEL */, [191] = BF_ILL | BF_NON, [255] = BF_ILL | BF_NON

};

/*
 * Escape sequence to byte mapping and vice versa. Apart from the quotes and escape character,
 * this is the minimal escape sequence set specified by JSON, so let's be nice here.
 * Each of these must carry either BF_ESC or BF_ESS flags above. Otherwise, Alabama Song:
 *
 * I tell you we must die, I tell you we must die,
 * I tell you, I tell you, I tell you we must die.
 *
 */
static const char esccodes[] = {

    /* these... */

    ['\b'] = 'b', /* BS  */
    ['\t'] = 't', /* TAB */
    ['\n'] = 'n', /* LF  */
    ['\f'] = 'f', /* FF  */
    ['\r'] = 'r', /* CR  */

    /* do not collide with these... */

    ['b'] = '\b', /* BS  */
    ['t'] = '\t', /* TAB */
    ['n'] = '\n', /* LF  */
    ['f'] = '\f', /* FF  */
    ['r'] = '\r', /* CR  */

    /* ...or these. Also, see what I did there? */

    [BS_ESCAPE_CHAR  ] = BS_ESCAPE_CHAR,
    [BS_QUOTE_CHAR] = BS_QUOTE_CHAR,
#ifdef BS_QUOTE1_CHAR
    [BS_QUOTE1_CHAR] = BS_QUOTE1_CHAR,
#endif
#ifdef BS_QUOTE2_CHAR
    [BS_QUOTE2_CHAR] = BS_QUOTE2_CHAR,
#endif
#ifdef BS_QUOTE3_CHAR
    [BS_QUOTE3_CHAR] = BS_QUOTE3_CHAR,
#endif

    /* Juniper does this... */
    ['['] = '[',
    [']'] = ']',
};

#endif /* BARSER_DEFAULTS_H_ */
