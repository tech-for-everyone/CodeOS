"""
Ziggy AI Engine — uses Jarvis as the AI reasoning engine.

This is a compatibility layer that provides the same interface as Ziggy's
original rule-based engine, but delegates to Jarvis.Jarvis() under the hood.
"""

import sys
import os

# Add Jarvis to path
sys.path.insert(0, '/home/codeosuser/CodeOS/jarvis')
from jarviscli import Jarvis

_jarvis_instance = None

def get_jarvis():
    global _jarvis_instance
    if _jarvis_instance is None:
        _jarvis_instance = Jarvis.Jarvis()
    return _jarvis_instance

def query(prompt):
    """Process a prompt through Jarvis and return the response."""
    try:
        j = get_jarvis()
        if hasattr(j, 'executor') and callable(j.executor):
            result = j.executor(prompt)
            return result if result else ""
        elif hasattr(j, 'say') and callable(j.say):
            # say method might not return anything useful
            return str(prompt)  # fallback
        else:
            return str(prompt)
    except Exception as e:
        return f"Error: {str(e)}"

def get_response(prompt):
    """Get AI response for a prompt (compatible interface)."""
    return query(prompt)
