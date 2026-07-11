# Cooperative Browser Integration

This optional Chrome/Edge Manifest V3 extension uses native messaging. It never closes a browser process. A tab can be discarded only when the user approved its tab ID and it is inactive, unpinned, inaudible, not sharing media, web-origin, not already discarded, and browser-discardable.

`discard_tab` stores a transaction record before calling `chrome.tabs.discard`. `restore_tab` reloads the exact tab and removes the transaction. The native host manifest must be installed with the final extension ID and absolute host path; missing integration leaves core safety unchanged.
