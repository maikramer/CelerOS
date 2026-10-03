#!/usr/bin/env python3
"""Compatibilidade: provisiona a chave da DeepSeek (wrapper do push_ai_key).

    python3 tools/push_deepseek_key.py [-p /dev/ttyUSB0]
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.exit(subprocess.call(
    [sys.executable, os.path.join(HERE, "push_ai_key.py"), "deepseek"] + sys.argv[1:]))
