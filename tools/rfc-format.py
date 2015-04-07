#!/usr/bin/python
#
# Takes an .nroff source file and prints a text file in RFC format.
#
# Usage: rfc-format.py <source file>

import re
import sys
from subprocess import Popen, PIPE


def Readfile(fn):
  f = open(fn, "r")
  return f.read()


def FixNroffOutput(buf):
  p = re.compile(r'(.*)FORMFEED(\[Page\s+\d+\])$')
  strip_empty = False
  out = ""
  for line in buf.split("\n"):
    line = line.replace("\xe2\x80\x99", "'")
    line = line.replace("\xe2\x80\x90", "-")
    for i in range(len(line)):
      if ord(line[i]) > 128:
        print >>sys.stderr, "Invalid character %d\n" % ord(line[i])
    m = p.search(line)
    if strip_empty and len(line) == 0:
      continue
    if m:
     out += p.sub(r'\1        \2\n\f', line)
     out += "\n"
     strip_empty = True
    else:
      out += "%s\n" % line
      strip_empty = False
  return out.rstrip("\n")


def Nroff(buf):
  p = Popen(["nroff", "-ms"], stdin=PIPE, stdout=PIPE)
  out, err = p.communicate(input=buf)
  return FixNroffOutput(out)


def FormatTocLine(section, title, page):
  line = ""
  level = 1
  if section:
    level = section.count(".")
  for i in range(level):
    line += "   "
  if section:
    line += "%s  " % section
  line += "%s " % title
  pagenum = "%d" % page
  nspace = 72 - len(line) - len(pagenum)
  if nspace % 2:
    line += " "
  for i in range(nspace / 2):
    line += ". "
  line += "%d\n" % page
  return line


def CreateToc(buf):
  p1 = re.compile(r'^((\d+\.)+)\s+(.*)$')
  p2 = re.compile(r'^(Appendix [A-Z].)\s+(.*)$')
  p3 = re.compile(r'\[Page (\d+)\]$')
  found = 0
  page = 1
  out = ""
  for line in buf.split("\n"):
    m1 = p1.search(line)
    m2 = p2.search(line)
    m3 = p3.search(line)
    if m1:
      out += FormatTocLine(m1.group(1), m1.group(3), page)
    elif m2:
      out += FormatTocLine(m2.group(1), m2.group(2), page)
    elif line.startswith("Authors"):
      out += FormatTocLine(None, line, page)
    elif m3:
      page = int(m3.group(1)) + 1
  return out


src = Readfile(sys.argv[1])
out = Nroff(src)
toc = CreateToc(out)
src = src.replace("INSERT_TOC_HERE", toc)
print Nroff(src)
