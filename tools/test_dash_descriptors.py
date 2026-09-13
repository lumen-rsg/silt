#!/usr/bin/env python3
"""Run shared descriptor exhaustion cases under a private Linux fd limit."""
from test_dash_jobs import main

if __name__ == '__main__':
    main(descriptor_mode=True)
