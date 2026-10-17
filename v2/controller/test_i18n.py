#!/usr/bin/env python3
"""Web pages in English and French: every English text has its French version.

For data/index.html and data/system.html:
- each t("...") / tn(n, "...", "...") text and each data-t element (content or attribute) has an entry
  in the page's FR dictionary, with the same {placeholders} and the same markup;
- the controller's texts shown by the page have one too: race messages of core.py on the race page,
  API errors of esp32.py / pc.py on the system page;
- FR entries nothing uses anymore are only reported.
"""
import json, os, re
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
STR = r'"((?:[^"\\]|\\.)*)"'                       # double-quoted JS / Python string
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}


def read(path):
    with open(os.path.join(HERE, path), encoding="utf-8") as f:
        return f.read()


def unquote(s):
    return json.loads('"' + s + '"')


def norm(s):                                       # as the page does: trim, collapse white space
    return " ".join(s.split())


class Marked(HTMLParser):
    """Texts of the data-t elements, as the browser gives them: (target, text), target "html" = content."""
    def __init__(self, src):
        super().__init__(convert_charrefs=False)
        self.src, self.found, self.stack, self.starts = src, [], [], [0]
        for line in src.splitlines(keepends=True):
            self.starts.append(self.starts[-1] + len(line))

    def at(self):
        line, col = self.getpos()
        return self.starts[line - 1] + col

    def handle_starttag(self, tag, attrs):
        a, content = dict(attrs), None
        if "data-t" in a:
            for target in (a["data-t"] or "html").split():
                if target == "html":
                    content = self.at() + len(self.get_starttag_text())
                else:
                    self.found.append((target, a.get(target) or ""))
        if tag not in VOID:
            self.stack.append((tag, content))

    def handle_endtag(self, tag):
        while self.stack:
            open_tag, content = self.stack.pop()
            if content is not None:
                self.found.append(("html", self.src[content:self.at()]))
            if open_tag == tag:
                break


def check(page, server):
    src = read(page)
    block = re.search(r"const FR = \{(.*?)\n\};", src, re.S).group(1)
    pairs = [(unquote(k), unquote(v)) for k, v in re.findall(STR + r"\s*:\s*" + STR, block)]
    fr = dict(pairs)
    assert len(fr) == len(pairs), "%s: key given twice: %s" % (page, [k for k, _ in pairs if [p[0] for p in pairs].count(k) > 1])
    used = {unquote(s) for s in re.findall(r"(?<![\w$.])t\(\s*" + STR, src)}
    for one, many in re.findall(r"(?<![\w$.])tn\([^,()]+,\s*" + STR + r"\s*,\s*" + STR, src):
        used |= {unquote(one), unquote(many)}
    parser = Marked(src)
    parser.feed(src)
    marked = {norm(text) for _, text in parser.found}
    missing = sorted(k for k in used | marked | server if k not in fr)
    assert not missing, "%s: no French for:\n  %s" % (page, "\n  ".join(missing))
    for k, v in fr.items():
        assert sorted(re.findall(r"\{\w+\}", k)) == sorted(re.findall(r"\{\w+\}", v)), "%s: placeholders differ: %r" % (page, k)
        assert sorted(re.findall(r"<[^>]*>", k)) == sorted(re.findall(r"<[^>]*>", v)), "%s: markup differs: %r" % (page, k)
    unused = sorted(set(fr) - used - marked - server)
    print("%-11s: OK %d texts in French (%d in the page, %d in the code, %d from the controller)%s" % (
        page.split("/")[-1].split(".")[0], len(fr), len(marked), len(used), len(server),
        "" if not unused else "; unused: " + ", ".join(map(repr, unused))))


def server_texts(paths, patterns):
    out = set()
    for path in paths:
        code = read(path)
        for p in patterns:
            for s in re.findall(p, code):
                out.add(unquote(s).replace("%d", "{n}").rstrip(": "))    # "path refused: " + path -> prefix
    return out


race = server_texts(["ricino/core.py"], [r"set_message\(\s*" + STR, r'doc\["race"\] = \{"message":\s*' + STR])
api = server_texts(["ricino/esp32.py", "ricino/pc.py"], [r'\{"error":\s*' + STR, r"raise (?:OSError|ValueError)\(\s*" + STR])
assert "Start in {n} s" in race and "path refused" in api and "invalid size ({n} bytes max)" in api, (race, api)
check("data/index.html", race)
check("data/system.html", api)
