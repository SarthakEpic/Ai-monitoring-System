const HOST = "com.predictive_autoheal.browser";
const POLL_MS = 2000;
let port = null;
let timer = null;

async function tabState(tab) {
  const media = await chrome.storage.local.get([`media:${tab.id}`]);
  return {
    tabId: tab.id,
    url: tab.url || "",
    active: Boolean(tab.active),
    pinned: Boolean(tab.pinned),
    audible: Boolean(tab.audible),
    muted: Boolean(tab.mutedInfo?.muted),
    discarded: Boolean(tab.discarded),
    autoDiscardable: tab.autoDiscardable !== false,
    sharingMedia: Boolean(media[`media:${tab.id}`])
  };
}

function safeToDiscard(tab, approvedIds) {
  const isWeb = tab.url.startsWith("https://") || tab.url.startsWith("http://");
  return approvedIds.includes(tab.tabId) && isWeb && !tab.active && !tab.pinned &&
    !tab.audible && !tab.sharingMedia && !tab.discarded && tab.autoDiscardable;
}

async function handleCommand(message) {
  if (!message || message.command === "poll") return;
  if (message.command === "inspect") {
    const tabs = await chrome.tabs.query({});
    port?.postMessage({ type: "tab_snapshot", requestId: message.requestId,
      tabs: await Promise.all(tabs.map(tabState)) });
    return;
  }
  if (message.command === "discard_tab") {
    const approved = await chrome.storage.local.get(["approvedTabIds"]);
    const tab = await chrome.tabs.get(message.tabId);
    const state = await tabState(tab);
    if (!safeToDiscard(state, approved.approvedTabIds || [])) {
      port?.postMessage({ type: "action_result", transactionId: message.transactionId,
        status: "BLOCKED", reason: "tab failed active/pinned/audible/media/approval safety gates" });
      return;
    }
    await chrome.storage.local.set({ [`transaction:${message.transactionId}`]: { tabId: tab.id, url: tab.url } });
    await chrome.tabs.discard(tab.id);
    port?.postMessage({ type: "action_result", transactionId: message.transactionId,
      status: "EXECUTED", reversible: true });
    return;
  }
  if (message.command === "restore_tab") {
    const key = `transaction:${message.transactionId}`;
    const saved = await chrome.storage.local.get([key]);
    if (!saved[key]) {
      port?.postMessage({ type: "action_result", transactionId: message.transactionId,
        status: "RESTORE_FAILED", reason: "transaction not found" });
      return;
    }
    await chrome.tabs.reload(saved[key].tabId);
    await chrome.storage.local.remove([key]);
    port?.postMessage({ type: "action_result", transactionId: message.transactionId,
      status: "ROLLED_BACK", restored: true });
  }
}

function connect() {
  try {
    port = chrome.runtime.connectNative(HOST);
    port.onMessage.addListener(message => handleCommand(message).catch(error =>
      port?.postMessage({ type: "extension_error", reason: String(error) })));
    port.onDisconnect.addListener(() => { port = null; setTimeout(connect, 5000); });
    clearInterval(timer);
    timer = setInterval(() => port?.postMessage({ type: "poll", version: 1 }), POLL_MS);
  } catch (_) {
    port = null;
    setTimeout(connect, 5000);
  }
}

chrome.runtime.onInstalled.addListener(() => chrome.storage.local.set({ approvedTabIds: [] }));
connect();
