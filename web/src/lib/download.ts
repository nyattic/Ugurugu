export function downloadBlob(blob: Blob, filename: string) {
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = filename;
    // In the document because some browsers ignore click() on a detached
    // anchor, and the URL outlives the call because revoking it in the same
    // turn can cancel a download that has not started reading yet.
    anchor.style.display = "none";
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
}
