"""Wire format definitions and pure parser.

No I/O, no presentation. See DOCS/Error_handling.md §5.
"""

# Severity values match firmware enum order.
SEV_INFO, SEV_WARN, SEV_ERR, SEV_FATAL = 0, 1, 2, 3
SEV_FROM_STR = {"INFO": 0, "WARN": 1, "ERR": 2, "FATAL": 3}

CLASSES = ("BUS", "TX", "NODE", "FRAME", "TIMING", "SYSTEM")


class ParsedLine:
    """One wire-format line, tokenized."""
    __slots__ = ("kind", "fields", "raw")

    def __init__(self, kind: str, fields: dict, raw: str):
        self.kind = kind
        self.fields = fields
        self.raw = raw


def parse_line(line: str) -> ParsedLine:
    """Parse one line into kind + key=value dict.

    Whitespace-tolerant. Quoted strings preserve internal spaces.
    Returns kind=UNKNOWN on blank/garbled lines (never raises).
    """
    s = line.strip()
    if not s:
        return ParsedLine("UNKNOWN", {}, line)
    tokens = _tokenize(s)
    if not tokens:
        return ParsedLine("UNKNOWN", {}, line)
    kind = tokens[0].upper()
    fields: dict[str, str] = {}
    for tok in tokens[1:]:
        if "=" in tok:
            k, _, v = tok.partition("=")
            fields[k] = v.strip('"')
    return ParsedLine(kind, fields, line)


def _tokenize(s: str) -> list:
    """Whitespace-split with double-quote awareness."""
    out: list = []
    buf: list = []
    in_q = False
    for ch in s:
        if ch == '"':
            in_q = not in_q
            buf.append(ch)
        elif ch.isspace() and not in_q:
            if buf:
                out.append("".join(buf))
                buf = []
        else:
            buf.append(ch)
    if buf:
        out.append("".join(buf))
    return out


# ----- Command builders ----------------------------------------------------

def cmd_set_level(n: int) -> bytes:
    return f"SET level={int(n)}\n".encode("ascii")


def cmd_reset_counters() -> bytes:
    return b"RESET counters\n"


def cmd_reset_events() -> bytes:
    return b"RESET events\n"


def cmd_ping() -> bytes:
    return b"PING\n"


def cmd_dump_config() -> bytes:
    return b"DUMP config\n"
