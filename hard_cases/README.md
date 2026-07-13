# Aegis-99 hard-case corpus

`python/stress_lab.py` deterministically emits versioned, harmless adversarial
sequences for regression testing. It does not pretend to replace real Windows
stress, hardware-matrix, or soak evidence.

Every confirmed production failure must be added as a new corpus case and
covered by an automated regression test before a new release candidate is
frozen.
