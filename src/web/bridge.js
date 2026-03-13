/**
 * BANDPASS II — JS → C++ message bridge
 * Provides a single postMessage(obj) function that works across
 * WebKit2GTK (Linux), WebKit (macOS), and WebView2 (Windows).
 */
(function () {
    'use strict';

    window.bandpassPostMessage = function (obj) {
        var msg = JSON.stringify(obj);
        // wxWebView AddScriptMessageHandler("bandpass") path (WebKit2GTK + WebKit)
        if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.bandpass) {
            window.webkit.messageHandlers.bandpass.postMessage(msg);
            return;
        }
        // WebView2 (Windows) path
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(msg);
            return;
        }
        // Fallback: wx JS bridge (old wx path)
        if (window.bandpass && typeof window.bandpass.postMessage === 'function') {
            window.bandpass.postMessage(msg);
            return;
        }
        // Last resort: console log for debugging
        console.log('[bandpass bridge] no backend available, message dropped:', msg);
    };
}());
