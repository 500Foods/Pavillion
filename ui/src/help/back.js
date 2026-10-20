/* D-10 (D-Q18): Help's Back goes one step back to the last tab shown
   before Help: the module whose Help jump opened it, or the last panel when
   Help came from the nav. note() takes each newly active tab snapshot. */
export function createBackTracker(helpSection = "help") {
  let last = null;
  return {
    note(tab) {
      if (tab && tab.section !== helpSection) {
        last = { id: tab.id, section: tab.section };
      }
    },
    /* The tab to return to, if it is still open. */
    target(openTabs) {
      if (!last || !Array.isArray(openTabs)) {
        return null;
      }
      return openTabs.some((tab) => tab.id === last.id) ? last : null;
    },
    clear() {
      last = null;
    },
  };
}
