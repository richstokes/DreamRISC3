#!/usr/bin/env python3
"""Tokenise BBC BASIC V text into the format SAVE writes.

BASIC can load text itself, but it then prints "Program renumbered" over the
desktop and takes a while. Lines get numbered 10, 20, ... so programs can't GOTO.
"""
import re
import sys

TOKENS = {
    "OTHERWISE": b"\x7f", "AND": b"\x80", "DIV": b"\x81", "EOR": b"\x82", "MOD": b"\x83",
    "OR": b"\x84", "ERROR": b"\x85", "LINE": b"\x86", "OFF": b"\x87", "STEP": b"\x88",
    "SPC": b"\x89", "TAB(": b"\x8a", "ELSE": b"\x8b", "THEN": b"\x8c", "OPENIN": b"\x8e",
    "ABS": b"\x94", "ACS": b"\x95", "ADVAL": b"\x96", "ASC": b"\x97", "ASN": b"\x98",
    "ATN": b"\x99", "BGET": b"\x9a", "COS": b"\x9b", "COUNT": b"\x9c", "DEG": b"\x9d",
    "ERL": b"\x9e", "ERR": b"\x9f", "EVAL": b"\xa0", "EXP": b"\xa1", "EXT": b"\xa2",
    "FALSE": b"\xa3", "FN": b"\xa4", "GET": b"\xa5", "INKEY": b"\xa6", "INSTR(": b"\xa7",
    "INT": b"\xa8", "LEN": b"\xa9", "LN": b"\xaa", "LOG": b"\xab", "NOT": b"\xac",
    "OPENUP": b"\xad", "OPENOUT": b"\xae", "PI": b"\xaf", "POINT(": b"\xb0", "POS": b"\xb1",
    "RAD": b"\xb2", "RND": b"\xb3", "SGN": b"\xb4", "SIN": b"\xb5", "SQR": b"\xb6",
    "TAN": b"\xb7", "TO": b"\xb8", "TRUE": b"\xb9", "USR": b"\xba", "VAL": b"\xbb",
    "VPOS": b"\xbc", "CHR$": b"\xbd", "GET$": b"\xbe", "INKEY$": b"\xbf", "LEFT$(": b"\xc0",
    "MID$(": b"\xc1", "RIGHT$(": b"\xc2", "STR$": b"\xc3", "STRING$(": b"\xc4", "EOF": b"\xc5",
    "WHEN": b"\xc9", "OF": b"\xca", "ENDCASE": b"\xcb", "ENDIF": b"\xcd", "ENDWHILE": b"\xce",
    "SOUND": b"\xd4", "BPUT": b"\xd5", "CALL": b"\xd6", "CHAIN": b"\xd7", "CLEAR": b"\xd8",
    "CLOSE": b"\xd9", "CLG": b"\xda", "CLS": b"\xdb", "DATA": b"\xdc", "DEF": b"\xdd",
    "DIM": b"\xde", "DRAW": b"\xdf", "END": b"\xe0", "ENDPROC": b"\xe1", "ENVELOPE": b"\xe2",
    "FOR": b"\xe3", "GOSUB": b"\xe4", "GOTO": b"\xe5", "GCOL": b"\xe6", "IF": b"\xe7",
    "INPUT": b"\xe8", "LET": b"\xe9", "LOCAL": b"\xea", "MODE": b"\xeb", "MOVE": b"\xec",
    "NEXT": b"\xed", "ON": b"\xee", "VDU": b"\xef", "PLOT": b"\xf0", "PRINT": b"\xf1",
    "PROC": b"\xf2", "READ": b"\xf3", "REM": b"\xf4", "REPEAT": b"\xf5", "REPORT": b"\xf6",
    "RESTORE": b"\xf7", "RETURN": b"\xf8", "RUN": b"\xf9", "STOP": b"\xfa", "COLOUR": b"\xfb",
    "TRACE": b"\xfc", "UNTIL": b"\xfd", "WIDTH": b"\xfe", "OSCLI": b"\xff",
    "SUM": b"\xc6\x8e", "BEAT": b"\xc6\x8f",
    "CASE": b"\xc8\x8e", "CIRCLE": b"\xc8\x8f", "FILL": b"\xc8\x90", "ORIGIN": b"\xc8\x91",
    "POINT": b"\xc8\x92", "RECTANGLE": b"\xc8\x93", "SWAP": b"\xc8\x94", "WHILE": b"\xc8\x95",
    "WAIT": b"\xc8\x96", "MOUSE": b"\xc8\x97", "QUIT": b"\xc8\x98", "SYS": b"\xc8\x99",
    "INSTALL": b"\xc8\x9a", "LIBRARY": b"\xc8\x9b", "TINT": b"\xc8\x9c", "ELLIPSE": b"\xc8\x9d",
    "BEATS": b"\xc8\x9e", "TEMPO": b"\xc8\x9f", "VOICES": b"\xc8\xa0", "VOICE": b"\xc8\xa1",
    "STEREO": b"\xc8\xa2", "OVERLAY": b"\xc8\xa3",
}
KEYWORDS = sorted(TOKENS, key=len, reverse=True)
LINE_START_ELSE = b"\xcc"
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*[%$]?")
HEX = re.compile(r"&[0-9A-Fa-f]*")


def tokenise_line(text):
    out = bytearray()
    i = 0
    leading = len(text) - len(text.lstrip(" "))
    while i < len(text):
        c = text[i]
        if c == '"':
            end = text.find('"', i + 1)
            end = len(text) if end < 0 else end + 1
            out += text[i:end].encode("latin-1")
            i = end
            continue
        if c == "&":
            match = HEX.match(text, i)
            out += match.group().encode("latin-1")
            i = match.end()
            continue
        keyword = next((k for k in KEYWORDS if text.startswith(k, i)), None)
        if keyword:
            if keyword == "ELSE" and i == leading:
                out += LINE_START_ELSE
            else:
                out += TOKENS[keyword]
            i += len(keyword)
            if keyword in ("REM", "DATA"):
                out += text[i:].encode("latin-1")
                break
            if keyword in ("PROC", "FN"):
                match = IDENT.match(text, i)
                if match:
                    out += match.group().encode("latin-1")
                    i = match.end()
            continue
        match = IDENT.match(text, i)
        if match:
            out += match.group().encode("latin-1")
            i = match.end()
            continue
        out.append(ord(c))
        i += 1
    return bytes(out)


def tokenise(source):
    out = bytearray()
    number = 10
    for line in source.splitlines():
        body = tokenise_line(line)
        assert len(body) + 4 < 256, f"line too long: {line}"
        out += bytes((13, number >> 8, number & 255, len(body) + 4)) + body
        number += 10
    return bytes(out + b"\r\xff")


if __name__ == "__main__":
    sys.stdout.buffer.write(tokenise(open(sys.argv[1], encoding="latin-1").read()))
