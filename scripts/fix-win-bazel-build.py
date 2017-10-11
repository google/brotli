import fnmatch
import os
import os.path
from shutil import copyfile

matches = []
for root, dirnames, filenames in os.walk('bazel-bin\\java\\org\\brotli'):
  for filename in fnmatch.filter(filenames, '*.runfiles_manifest'):
    matches.append(os.path.join(root, filename))

for match in matches:
  runfiles = match[:-len('_manifest')]
  with open(match) as manifest:
    for entry in manifest:
      entry = entry.strip()
      if not entry.startswith("org_brotli"):
        continue
      if entry.startswith('org_brotli/external'):
        continue
      (alias, space, link) = entry.partition(' ')
      if alias.endswith('.jar') or alias.endswith('.exe'):
        continue
      link = link.replace('/', '\\')
      alias = alias.replace('/', '\\')
      dst = os.path.join(runfiles, alias)
      if not os.path.exists(dst):
        print(link + ' -> ' + dst)
        parent = os.path.dirname(dst)
        if not os.path.exists(parent):
          os.makedirs(parent)
        copyfile(link, dst)
