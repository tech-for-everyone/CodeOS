"""
Ziggy AI Models — data for the rule-based AI system.

This module is kept for compatibility but the actual AI reasoning
is now delegated to Jarvis.Jarvis() in the backend server.
"""

# Legacy rule data - kept for backward compatibility
# The actual rule engine is now in Jarvis, but we maintain these
# for any code that might import from this module.

# Original Ziggy ELIZA-style rules (placeholder)
RULES = []

# Rule patterns (AND/OR/notand format)
# These are kept for compatibility; Jarvis handles actual AI
AND = "AND"
OR = "OR"
NOTAND = "NOTAND"

# Persona configurations
PERSONAS = {
    "freecode": "FreeCode",
    "ziggy": "Ziggy",
}

# Default persona
DEFAULT_PERSONA = "ziggy"
