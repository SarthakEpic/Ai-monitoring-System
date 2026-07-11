async function render() {
  const tabs = await chrome.tabs.query({});
  const stored = await chrome.storage.local.get(["approvedTabIds"]);
  const approved = new Set(stored.approvedTabIds || []);
  const root = document.querySelector("#tabs");
  root.replaceChildren();
  for (const tab of tabs) {
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = approved.has(tab.id);
    input.disabled = tab.active || tab.pinned || tab.audible || !/^https?:/.test(tab.url || "");
    input.addEventListener("change", async () => {
      input.checked ? approved.add(tab.id) : approved.delete(tab.id);
      await chrome.storage.local.set({ approvedTabIds: [...approved] });
    });
    label.append(input, document.createTextNode(` ${tab.title || tab.url}`));
    root.append(label, document.createElement("br"));
  }
}
document.querySelector("#refresh").addEventListener("click", render);
render();
