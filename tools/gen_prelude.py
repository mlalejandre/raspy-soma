#!/usr/bin/env python3
"""Genera src/apps/web_js_prelude.h a partir de tools/web_prelude.js (uso: python3 tools/gen_prelude.py [raiz])."""
import sys, os
root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = open(os.path.join(root, 'tools', 'web_prelude.js'), encoding='utf-8').read()
out = ['/* generado desde tools/web_prelude.js - no editar a mano */', 'const char WEB_PRELUDE[] =']
for l in src.split('\n'):
    out.append('"' + l.replace('\\', '\\\\').replace('"', '\\"') + '\\n"')
out[-1] += ';'
open(os.path.join(root, 'src', 'apps', 'web_js_prelude.h'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
