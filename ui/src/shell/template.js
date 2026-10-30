// html-ok: static shell markup. No outside data is interpolated.
export const shellMarkup = `<div class="shell">
      <nav class="nav" id="nav" aria-label="Sections">
        <div class="nav-buttons button-bar" role="toolbar" aria-label="Sections">
          <button type="button" data-section="mail" data-rail="mail" aria-pressed="true" aria-label="Mail">
            <img class="nav-mark" alt="">
            <img class="icon" src="pav-system://mark" alt="" data-icon-key="nav.mail">
          </button>
          <button type="button" data-section="contacts" data-rail="contacts" aria-pressed="false" aria-label="Contacts">
            <i class="icon fa-sharp-duotone fa-solid fa-address-book" data-icon-key="nav.contacts" data-icon-definition="fa-sharp-duotone fa-solid fa-address-book" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="calendars" data-rail="calendars" aria-pressed="false" aria-label="Calendars">
            <i class="icon fa-sharp-duotone fa-solid fa-calendar-days" data-icon-key="nav.calendars" data-icon-definition="fa-sharp-duotone fa-solid fa-calendar-days" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="tasks" data-rail="tasks" aria-pressed="false" aria-label="Tasks" hidden>
            <i class="icon fa-sharp-duotone fa-solid fa-list-check" data-icon-key="nav.tasks" data-icon-definition="fa-sharp-duotone fa-solid fa-list-check" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="files" data-rail="files" aria-pressed="false" aria-label="Files">
            <i class="icon fa-sharp-duotone fa-solid fa-folder" data-icon-key="nav.files" data-icon-definition="fa-sharp-duotone fa-solid fa-folder" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="notes" data-rail="notes" aria-pressed="false" aria-label="Notes" hidden>
            <i class="icon fa-sharp-duotone fa-solid fa-notebook" data-icon-key="nav.notes" data-icon-definition="fa-sharp-duotone fa-solid fa-notebook" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="argent" data-rail="argent" aria-pressed="false" aria-label="Argent" hidden>
            <i class="icon fa-sharp-duotone fa-solid fa-coins" data-icon-key="nav.argent" data-icon-definition="fa-sharp-duotone fa-solid fa-coins" aria-hidden="true"></i>
          </button>
        </div>
        <div class="nav-body">
          <div data-nav="mail">
            <div class="tree-toolbar" id="mail-tree-toolbar" role="toolbar" aria-label="Mail tree">
              <button type="button" id="mail-tree-star" data-tree-filter="star" aria-pressed="false" aria-label="Show favorites only">
                <i class="icon fa-sharp-duotone fa-solid fa-star" data-icon-key="mail.tree.star" aria-hidden="true"></i>
              </button>
              <button type="button" id="mail-tree-heart" data-tree-filter="heart" aria-pressed="false" aria-label="Show heart favorites only">
                <i class="icon fa-sharp-duotone fa-solid fa-heart" data-icon-key="mail.tree.heart" aria-hidden="true"></i>
              </button>
              <button type="button" id="mail-tree-ghost" data-tree-filter="ghost" aria-pressed="false" aria-label="Hide infrequent folders">
                <i class="icon fa-sharp-duotone fa-solid fa-cloud" data-icon-key="mail.tree.cloud" aria-hidden="true"></i>
              </button>
            </div>
            <div id="mail-tree" class="tree-root"></div>
          </div>
          <div data-nav="contacts" hidden>
            <div class="letters" id="letters-nav"></div>
            <input class="search" type="search" aria-label="Search contacts" />
            <p class="group-label">Accounts</p>
            <p class="empty">None</p>
            <p class="group-label">Recents</p>
            <p class="empty">None</p>
          </div>
          <div data-nav="calendars" hidden>
            <div class="weeks" id="mini-cal" aria-label="Calendar"></div>
            <p class="group-label">Account</p>
            <label class="swatch-row"><input type="checkbox" checked /> <span class="swatch swatch-1"></span> Personal</label>
            <label class="swatch-row"><input type="checkbox" checked /> <span class="swatch swatch-2"></span> Shared</label>
          </div>
          <div data-nav="tasks" hidden>
            <p class="empty">Tasks are not available yet.</p>
          </div>
          <div data-nav="files" hidden>
            <p class="empty">No folders.</p>
          </div>
          <div data-nav="notes" hidden>
            <p class="empty">Notes are not available yet.</p>
          </div>
          <div data-nav="argent" hidden>
            <p class="empty">Argent is not available yet.</p>
          </div>
          <div data-nav="settings" hidden>
            <div class="heading-list" role="list">
              <button type="button" data-heading="accounts">Accounts</button>
              <button type="button" data-heading="features" aria-current="true">Features</button>
              <button type="button" data-heading="theme">Themes</button>
              <button type="button" data-heading="icons">Icons</button>
              <button type="button" data-heading="libraries">Libraries</button>
              <button type="button" data-heading="assets">Assets</button>
              <button type="button" data-heading="fonts">Fonts</button>
              <button type="button" data-heading="formats">Formats</button>
              <button type="button" data-heading="tabs">Tabs</button>
              <button type="button" data-heading="zoom">Zooms</button>
              <button type="button" data-heading="log">Logs</button>
              <button type="button" data-heading="limits">Limits</button>
              <button type="button" data-heading="mail">Mail</button>
            </div>
          </div>
        </div>
        <div class="nav-buttons button-bar" role="toolbar" aria-label="Tools">
          <button type="button" data-section="bots" data-rail="bots" aria-pressed="false" aria-label="Grok Bots" hidden>
            <i class="icon fa-sharp fa-thin fa-ghost" data-icon-key="nav.bots" data-icon-definition="fa-sharp fa-thin fa-ghost" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="github" data-rail="github" aria-pressed="false" aria-label="GitHub" hidden>
            <i class="icon fa-brands fa-github" data-icon-key="nav.github" data-icon-definition="fa-brands fa-github" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="activity" data-rail="activity" aria-pressed="false" aria-label="Activity">
            <i class="icon fa-sharp-duotone fa-solid fa-list-timeline" data-icon-key="nav.activity" data-icon-definition="fa-sharp-duotone fa-solid fa-list-timeline" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="settings" data-rail="settings" aria-pressed="false" aria-label="Settings">
            <i class="icon fa-sharp-duotone fa-solid fa-gear" data-icon-key="nav.settings" data-icon-definition="fa-sharp-duotone fa-solid fa-gear" aria-hidden="true"></i>
          </button>
          <button type="button" id="update" data-rail="update" aria-label="Update">
            <span class="update-pie" hidden aria-hidden="true"></span>
            <i class="icon fa-sharp-duotone fa-solid fa-up-down" data-icon-key="nav.update" data-icon-definition="fa-sharp-duotone fa-solid fa-up-down" aria-hidden="true"></i>
            <span class="update-badge" hidden></span>
          </button>
          <button type="button" data-section="help" data-rail="help" aria-pressed="false" aria-label="Help">
            <i class="icon fa-sharp-duotone fa-solid fa-circle-question" data-icon-key="nav.help" data-icon-definition="fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
          </button>
        </div>
        <button type="button" class="nav-resize" id="nav-resize" aria-label="Resize navigation"></button>
      </nav>
      <div class="workspace">
        <div class="tabs button-bar" role="tablist" aria-label="Open views">
          <div id="tablist"></div>
          <div class="tabs-spacer" data-tauri-drag-region></div>
        </div>
        <p class="banner" id="banner" hidden></p>
        <div class="panel split" id="panel-mail" role="tabpanel">
          <div class="pane pane-list" id="mail-list"></div>
          <button type="button" class="split-resize" data-split="mail" aria-label="Resize mail panes"></button>
          <div class="pane pane-detail"></div>
        </div>
        <div class="panel split" id="panel-contacts" role="tabpanel" hidden>
          <div class="pane pane-list rolodex">
            <div class="letters letters-large" id="letters-main"></div>
          </div>
          <button type="button" class="split-resize" data-split="contacts" aria-label="Resize contact panes"></button>
          <div class="pane pane-detail"></div>
        </div>
        <div class="panel" id="panel-calendars" role="tabpanel" hidden>
          <div class="month" id="month"></div>
        </div>
        <div class="panel" id="panel-tasks" role="tabpanel" hidden>
          <p class="empty">Tasks are not available yet.</p>
        </div>
        <div class="panel split" id="panel-files" role="tabpanel" hidden>
          <div class="pane pane-list"></div>
          <button type="button" class="split-resize" data-split="files" aria-label="Resize file panes"></button>
          <div class="pane pane-detail"></div>
        </div>
        <div class="panel" id="panel-notes" role="tabpanel" hidden>
          <p class="empty">Notes are not available yet.</p>
        </div>
        <div class="panel" id="panel-argent" role="tabpanel" hidden>
          <p class="empty">Argent is not available yet.</p>
        </div>
        <div class="panel" id="panel-bots" role="tabpanel" hidden>
          <div class="button-bar bots-bar" role="toolbar" aria-label="Grok Bots">
            <span id="bots-add-host" class="bots-add-host">
              <button type="button" id="bots-add" disabled>Add</button>
            </span>
          </div>
          <div id="bots-transcript">
            <p class="empty">No bots yet.</p>
          </div>
        </div>
        <div class="panel" id="panel-github" role="tabpanel" hidden>
          <p class="empty">GitHub is not available yet.</p>
        </div>
        <div class="panel settings" id="panel-settings" role="tabpanel" hidden>
          <div data-settings="features">
            <div class="button-bar settings-bar" role="toolbar" aria-label="Features">
              <span class="settings-bar-title">Features</span>
              <span class="log-bar-spacer"></span>
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="features" aria-label="Help for Features">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <p class="muted">Show a button, hide it, or move it between the bars. Dragging an icon on the top or bottom bar does the same move: left or right stays in that bar, and crossing to the other bar moves it. A short press on the bar still opens the section. A check shows that button. Settings stays on. A shortcut is Ctrl and a number, and each number is used once.</p>
              <div id="feature-list" class="feature-list"></div>
            </div>
          </div>
          <div data-settings="zoom" hidden>
            <div class="button-bar settings-bar" role="toolbar" aria-label="Zooms">
              <span class="settings-bar-title">Zooms</span>
              <span class="log-bar-spacer"></span>
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="zoom" aria-label="Help for Zooms">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
            <div class="zoom-list">
              <label class="zoom" data-zoom="shell"><span class="zoom-label">Shell</span>
                <input id="zoom" type="range" min="0.5" max="2" step="0.05" value="1" />
                <span class="zoom-value" id="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="navButtons"><span class="zoom-label">Button bars</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="navBody"><span class="zoom-label">Nav body</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="mailList"><span class="zoom-label">Mail list</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="mailDetail"><span class="zoom-label">Mail detail</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="log"><span class="zoom-label">Log</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="helpIndex"><span class="zoom-label">Help index</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="helpContent"><span class="zoom-label">Help content</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="filters"><span class="zoom-label">Filters</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="tables"><span class="zoom-label">Tables</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="details"><span class="zoom-label">Details</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="settings"><span class="zoom-label">Settings</span>
                <input type="range" min="0.05" max="5" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="colorPickerZoom"><span class="zoom-label">Color picker</span>
                <input type="range" min="1" max="3" step="0.05" value="2" />
                <span class="zoom-value">200%</span>
              </label>
              <label class="zoom" data-zoom="iconWizardZoom"><span class="zoom-label">Icon Wizard</span>
                <input type="range" min="1" max="3" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <label class="zoom" data-zoom="iconResultZoom"><span class="zoom-label">Icon results</span>
                <input type="range" min="1" max="3" step="0.05" value="1" />
                <span class="zoom-value">100%</span>
              </label>
              <div class="zoom zoom-wheel">
                <span class="zoom-label">Wheel</span>
                <button type="button" class="zoom-wheel-switch" id="zoom-wheel" role="switch" aria-checked="false">Away zooms in</button>
                <span class="zoom-value" aria-hidden="true"></span>
              </div>
            </div>
            <p class="muted">Shell multiplies every row below except Color picker, Icon Wizard, and Icon results. Those three run from 100% to 300% and follow that control wherever it opens. Tables is the list on the left of Accounts, Icons, Libraries, and Assets. Details is the form on the right of Accounts, Libraries, and Assets. The Icons editor uses Icon Wizard. Icon results is the search grid inside that editor. Settings is the body of the other settings pages, including this one. Button bars covers the navigation, the main tabs, the log bars, the Help search bar, and the bars on settings pages. Hold Ctrl and roll the wheel over a button bar, the nav, the calendar, a mail pane, a table, a form, a settings page, the log, the Help index, or the Help page to change that row by 5%. The wheel leaves Shell alone. Away zooms in unless the switch says toward.</p>
            <p class="muted" id="profile-name"></p>
            </div>
          </div>
          <div data-settings="log" hidden>
            <div class="button-bar settings-bar" role="toolbar" aria-label="Logs">
              <span class="settings-bar-title">Logs</span>
              <span class="log-bar-spacer"></span>
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="log" aria-label="Help for Logs">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body log-settings">
              <label class="zoom"><span class="zoom-label">Size cap</span>
                <input id="log-cap" type="range" min="1" max="100" step="1" value="10" aria-label="Log size cap in MB" />
                <span class="zoom-value" id="log-cap-value">10 MB</span>
              </label>
              <p id="log-stats" aria-live="polite"></p>
              <p id="log-preview" class="muted" aria-live="polite"></p>
              <div class="log-settings-actions">
                <button type="button" id="log-refresh">Refresh</button>
                <button type="button" id="log-apply">Apply</button>
                <button type="button" id="log-trim-now">Trim Now</button>
              </div>
              <p class="muted">The log is kept in the profile's log database. Every level is kept. At shutdown, and on Apply or Trim Now, the oldest lines are removed until the database fits the cap. Apply saves the cap and trims to it. Trim Now trims to the saved cap.</p>
            </div>
          </div>
          <div data-settings="limits" hidden>
            <div class="button-bar pav-module-toolbar" id="limits-toolbar" role="toolbar" aria-label="Limits">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="limits" aria-label="Help for Limits">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <div class="limits-list">
                <label class="zoom"><span class="zoom-label">Database</span>
                  <input id="limits-bodies" type="number" min="1" max="1048576" step="1" value="2048" inputmode="numeric" />
                  <span class="zoom-value">MB</span>
                  <span id="limits-bodies-used" class="limits-used">0.0 MB on disk</span>
                </label>
                <label class="zoom"><span class="zoom-label">Document</span>
                  <input id="limits-blobs" type="number" min="1" max="1048576" step="1" value="4096" inputmode="numeric" />
                  <span class="zoom-value">MB</span>
                  <span id="limits-blobs-used" class="limits-used">0.0 MB on disk</span>
                </label>
                <label class="zoom"><span class="zoom-label">Message list</span>
                  <input id="limits-list-rows" type="number" min="1" max="10000" step="1" value="10000" inputmode="numeric" />
                  <span class="zoom-value">rows</span>
                </label>
                <label class="zoom"><span class="zoom-label">Recent icons</span>
                  <input id="limits-recent" type="number" min="1" max="48" step="1" value="12" inputmode="numeric" />
                  <span class="zoom-value">icons</span>
                </label>
              </div>
              <div class="limits-check">
                <button type="button" id="limits-measure">Check sizes</button>
                <progress id="limits-progress" class="limits-progress" hidden value="0" max="1" aria-label="Size check progress"></progress>
                <button type="button" id="limits-cancel" hidden>Cancel</button>
                <p id="limits-checked" class="limits-checked" aria-live="polite"></p>
              </div>
              <table class="limits-accounts" id="limits-accounts" hidden>
                <thead>
                  <tr>
                    <th>Account</th>
                    <th class="limits-num">Database<span class="limits-kind">(Mail, Contacts, Calendars)</span></th>
                    <th class="limits-num">Document<span class="limits-kind">(Attachments, Files)</span></th>
                  </tr>
                </thead>
                <tbody id="limits-account-rows"></tbody>
                <tfoot>
                  <tr>
                    <th scope="row">Total</th>
                    <td id="limits-total-database" class="limits-num"></td>
                    <td id="limits-total-document" class="limits-num"></td>
                  </tr>
                </tfoot>
              </table>
              <p class="muted">Database is message text, contacts, and calendars. Document is attachments and files. They stay on this machine up to these sizes. Beside each one is what is on disk now, and the free space on the volume that holds this storage. Each account lists how many of those entries are on the server, and their size. The Total row adds each column on its own. Database and Document are not added together. Database counts messages, contacts, and events. Document counts attachments and files. A size is shown after Check sizes has split mail into message text and attachments. A contact, calendar, or file size that was not reported is left out. A progress bar and Cancel show while that runs, and the line beside the button is when it last finished. Message list is how many messages a folder shows, newest first. It starts at 10000 and is a whole number from 1 to 10000. It is not a disk size. Recent icons is how many Icon Wizard picks are kept, from 1 to 48, starting at 12. Apply saves these limits. The next finished sync drops the oldest opened message text and documents that are over a limit. Drafts, flagged messages, and mail waiting to send stay. Contacts and calendars stay. The log size cap is on Logs.</p>
            </div>
            <div class="button-bar pav-module-footer" id="limits-footer" role="toolbar" aria-label="Limits actions"></div>
          </div>
          <div data-settings="mail" hidden>
            <div class="button-bar pav-module-toolbar" id="mail-toolbar" role="toolbar" aria-label="Mail">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="mail" aria-label="Help for Mail">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <label class="mail-css-extra">Extra CSS properties
                <textarea id="mail-css-extra" rows="8" spellcheck="false" aria-label="Extra CSS properties"></textarea>
              </label>
              <p class="muted">One property name per line. The built-in list stays fixed. A name starts with a lowercase letter and may continue with lowercase letters, digits, and hyphens. position, behavior, and a name that starts with a hyphen are refused. At most 32 names. Apply saves the list. An empty box clears it.</p>
            </div>
            <div class="button-bar pav-module-footer" id="mail-footer" role="toolbar" aria-label="Mail actions"></div>
          </div>
          <div data-settings="theme" hidden>
            <div class="button-bar settings-bar" role="toolbar" aria-label="Themes">
              <span class="settings-bar-title">Themes</span>
              <span class="log-bar-spacer"></span>
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="theme" aria-label="Help for Themes">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="theme-head">
              <p class="muted">Light, Dark, and System are built in. System follows the desktop, and Dark is used when that scheme cannot be read. A theme file is plain CSS. The row name is the Title inside the file. The same Title twice shows the filename in brackets. A selected theme file sets the rail icons. Settings → Icons Override puts this profile's icons back on Light, Dark, and System.</p>
            </div>
            <div class="button-bar theme-actions" role="toolbar" aria-label="Theme">
              <button type="button" id="theme-default">Default</button>
              <button type="button" id="theme-undo" disabled>Undo</button>
              <button type="button" id="theme-redo" disabled>Redo</button>
            </div>
            <p class="muted" id="theme-resolved">Following the system: dark.</p>
            <div id="theme-list" class="theme-list" role="radiogroup" aria-label="Theme"></div>
            <p class="muted" id="theme-folder-note" hidden>That folder is missing. Built-in themes are still available.</p>
            <div class="button-bar theme-bar" role="toolbar" aria-label="Themes">
              <button type="button" id="theme-refresh">Refresh</button>
              <label class="theme-folder">Themes folder
                <input id="theme-folder" type="text" spellcheck="false" autocomplete="off">
              </label>
              <button type="button" id="theme-folder-pick">Browse</button>
            </div>
          </div>
          <div data-settings="fonts" hidden>
            <div class="button-bar settings-bar" role="toolbar" aria-label="Fonts">
              <span class="settings-bar-title">Fonts</span>
              <span class="log-bar-spacer"></span>
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="fonts" aria-label="Help for Fonts">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
            <label class="font-row">Source
              <select id="font-source">
                <option value="theme" selected>Theme</option>
                <option value="override">Override</option>
              </select>
            </label>
            <label class="font-row">Body font
              <select id="body-font">
                <option value="&quot;Vanadium Sans&quot;, system-ui, sans-serif">Vanadium Sans</option>
                <option value="system-ui, sans-serif">System UI</option>
                <option value="ui-monospace, monospace">Monospace</option>
              </select>
            </label>
            <label class="font-row">Mono font
              <select id="mono-font">
                <option value="ui-monospace, monospace">System mono</option>
                <option value="&quot;Vanadium Mono&quot;, ui-monospace, monospace">Vanadium Mono</option>
                <option value="&quot;Liberation Mono&quot;, ui-monospace, monospace">Liberation Mono</option>
              </select>
            </label>
            <p class="muted">Vanadium Sans is the default body font. Packaging those font files is still open. Mono is used for logs and the icon fields.</p>
            </div>
          </div>
          <div data-settings="formats" hidden>
            <div class="button-bar pav-module-toolbar" id="formats-toolbar" role="toolbar" aria-label="Formats">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="formats" aria-label="Help for Formats">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body" id="formats-body"></div>
            <div class="button-bar pav-module-footer" id="formats-footer" role="toolbar" aria-label="Formats actions"></div>
          </div>
          <div data-settings="icons" hidden>
            <div class="button-bar pav-module-toolbar" id="icons-toolbar" role="toolbar" aria-label="Icons">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="icons" aria-label="Help for Icons">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <div class="settings-split">
                <div class="settings-tree-pane" id="icons-tree-pane">
                  <div id="icons-tree" class="settings-tree"></div>
                </div>
                <button type="button" class="split-resize" data-split="iconPane" aria-label="Resize icon panes"></button>
                <div class="settings-form-pane" id="icons-form"></div>
              </div>
            </div>
            <div class="button-bar pav-module-footer" id="icons-footer" role="toolbar" aria-label="Icon actions"></div>
          </div>
          <div data-settings="libraries" hidden>
            <div class="button-bar pav-module-toolbar" id="libraries-toolbar" role="toolbar" aria-label="Libraries">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="libraries" aria-label="Help for Libraries">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <div class="settings-split">
                <div class="settings-tree-pane" id="libraries-tree-pane">
                  <div id="libraries-tree" class="settings-tree"></div>
                </div>
                <button type="button" class="split-resize" data-split="libraryPane" aria-label="Resize library panes"></button>
                <div class="settings-form-pane" id="libraries-form"></div>
              </div>
            </div>
            <div class="button-bar pav-module-footer" id="libraries-footer" role="toolbar" aria-label="Library actions"></div>
          </div>
          <div data-settings="assets" hidden>
            <div class="button-bar pav-module-toolbar" id="assets-toolbar" role="toolbar" aria-label="Assets">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="assets" aria-label="Help for Assets">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <div class="settings-split">
                <div class="settings-tree-pane" id="assets-tree-pane">
                  <div id="assets-tree" class="settings-tree"></div>
                </div>
                <button type="button" class="split-resize" data-split="assetPane" aria-label="Resize asset panes"></button>
                <div class="settings-form-pane" id="assets-form"></div>
              </div>
            </div>
            <div class="button-bar pav-module-footer" id="assets-footer" role="toolbar" aria-label="Asset actions"></div>
          </div>
           <div data-settings="tabs" hidden>
             <div class="button-bar settings-bar" role="toolbar" aria-label="Tabs">
               <span class="settings-bar-title">Tabs</span>
               <span class="log-bar-spacer"></span>
               <button type="button" class="help-jump" data-help-page="tabs" data-help-section="restore" aria-label="Help for Tabs">
                 <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
               </button>
             </div>
             <div class="settings-body">
            <label class="swatch-row">
              <input id="session-restore" type="checkbox" checked />
              Restore tabs on launch
            </label>
            <p class="muted">On brings back the open tabs, including pins and the active tab. Off opens nothing, so the first nav button you click opens that section. The first launch opens Mail, unless Mail is hidden in Features. A clean quit with no tabs open stays empty. Closing the window and Ctrl+C both save the tabs. An unexpected shutdown opens nothing and says so.</p>
            <label class="swatch-row">
              Max tabs
              <input id="session-max" type="number" min="1" max="200" step="1" value="50" />
            </label>
            <p class="muted">The strip stops at this many tabs. Home tabs still open, so Settings stays reachable. A restart keeps every home tab, then the active tab and the pins, and leaves the rest closed. The default is 50.</p>
             </div>
           </div>
          <div data-settings="accounts" hidden>
            <div class="button-bar pav-module-toolbar" id="accounts-toolbar" role="toolbar" aria-label="Accounts">
              <button type="button" class="help-jump" data-help-page="settings" data-help-section="accounts" aria-label="Help for Accounts">
                <i class="icon fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
              </button>
            </div>
            <div class="settings-body">
              <div class="accounts-split">
                <div class="accounts-tree-pane">
                  <div id="accounts-tree" class="accounts-tree"></div>
                </div>
                <button type="button" class="split-resize" data-split="accounts" aria-label="Resize accounts panes"></button>
                <div class="accounts-form-pane">
                  <div id="accounts-form" class="accounts-form">
                    <div class="accounts-form-header" id="accounts-form-header">
                      <span id="account-form-swatch" class="accounts-form-swatch"></span>
                      <h2 class="accounts-form-title" id="account-form-title">Accounts</h2>
                      <span class="accounts-form-number" id="account-form-number"></span>
                    </div>
                    <form id="account-form" autocomplete="off">
                      <fieldset class="accounts-group" data-group="identity">
                        <legend>Identity</legend>
                        <label class="accounts-line"><span class="accounts-label">Display name</span>
                          <input id="account-name-input" type="text" spellcheck="true" autocomplete="off" />
                          <input id="account-color-input" class="accounts-color" type="text" value="#2a7" aria-label="Color" spellcheck="false" autocomplete="off" />
                          <button type="button" id="account-icon-choose" class="accounts-icon-choices" aria-label="Choose icon"></button>
                        </label>
                        <label class="accounts-line"><span class="accounts-label">Email</span>
                          <input id="account-email-input" type="email" spellcheck="false" autocomplete="off" placeholder="you@example.com" />
                        </label>
                      </fieldset>
                      <fieldset class="accounts-group" data-group="server">
                        <legend>Server</legend>
                        <label class="accounts-line"><span class="accounts-label">Kind</span>
                          <select id="account-kind">
                            <option value="jmap">JMAP</option>
                          </select>
                        </label>
                        <label class="accounts-line"><span class="accounts-label">Session URL</span>
                          <input id="account-session-url-input" type="url" spellcheck="false" autocomplete="off" placeholder="https://example.com/.well-known/jmap" />
                          <button type="button" id="account-detect" disabled>Detect</button>
                        </label>
                        <p class="accounts-line accounts-result"><span class="accounts-label"></span><span id="account-detect-status" class="accounts-test-status"></span><span id="account-detect-error" class="accounts-error"></span></p>
                        <div class="accounts-line"><span class="accounts-label">Auth method</span>
                          <span id="account-auth">basic</span>
                        </div>
                        <details class="accounts-advanced">
                          <summary>Advanced</summary>
                          <label class="accounts-line"><span class="accounts-label">CA file</span>
                            <input id="account-ca-file-input" type="text" spellcheck="false" autocomplete="off" placeholder="/path/to/ca.pem (blank: system store)" />
                          </label>
                        </details>
                      </fieldset>
                      <fieldset class="accounts-group" data-group="credentials">
                        <legend>Credentials</legend>
                        <label class="accounts-line"><span class="accounts-label">Username</span>
                          <input id="account-username-input" type="text" spellcheck="false" autocomplete="off" />
                        </label>
                        <label class="accounts-line"><span class="accounts-label">Password</span>
                          <input id="account-secret-input" type="password" autocomplete="new-password" />
                          <span id="account-credentials-status" class="accounts-saved">Not saved yet</span>
                        </label>
                      </fieldset>
                      <fieldset class="accounts-group" data-group="options">
                        <legend>Options</legend>
                        <div class="accounts-line"><span class="accounts-label">Sync</span>
                          <label><input id="account-sync-mail" type="checkbox" checked /> Mail</label>
                          <label><input id="account-sync-contacts" type="checkbox" checked /> Contacts</label>
                          <label><input id="account-sync-calendars" type="checkbox" checked /> Calendars</label>
                        </div>
                        <label class="accounts-line"><span class="accounts-label">Fetch bodies</span>
                          <select id="account-fetch-bodies">
                            <option value="recent">Recent</option>
                            <option value="all">All</option>
                            <option value="none">None</option>
                          </select>
                        </label>
                        <label class="accounts-line"><span class="accounts-label">Enabled</span>
                          <input id="account-enabled" type="checkbox" checked />
                        </label>
                      </fieldset>
                      <div class="accounts-line accounts-form-actions">
                        <span class="accounts-label"></span>
                        <button type="button" id="account-test" disabled>Test</button>
                        <button type="button" id="account-add">Add</button>
                        <button type="button" id="account-save" disabled>Save</button>
                        <button type="button" id="account-remove" disabled>Remove</button>
                        <span id="account-test-status" class="accounts-test-status"></span>
                        <span id="account-url-note" class="accounts-test-status"></span>
                      </div>
                      <p class="accounts-line accounts-result"><span class="accounts-label"></span><span id="account-test-error" class="accounts-error"></span><span id="account-test-capabilities" class="accounts-capabilities"></span></p>
                    </form>

                  </div>
                </div>
              </div>
            </div>
            <div class="button-bar pav-module-footer" id="accounts-footer" role="toolbar" aria-label="Account actions"></div>
          </div>
        </div>
        <div class="panel" id="panel-activity" role="tabpanel" hidden>
          <div class="button-bar log-topbar" role="toolbar" aria-label="Log tools">
            <div class="log-switch" role="tablist" aria-label="Activity views">
              <button type="button" role="tab" data-activity-view="activity" aria-selected="false">Activity</button>
              <button type="button" role="tab" data-activity-view="log" aria-selected="true">Logs</button>
            </div>
          </div>
          <div class="log-pane" data-activity-pane="activity" hidden></div>
          <div class="log-pane" data-activity-pane="log"></div>
          <div class="button-bar log-bottombar" role="toolbar" aria-label="Log actions"></div>
        </div>
        <div class="panel help-panel" id="panel-help" role="tabpanel" hidden>
          <div class="button-bar help-bar pav-module-toolbar" id="help-toolbar" role="toolbar" aria-label="Help">
            <button type="button" id="isolation-test" hidden>Show isolation test</button>
            <button type="button" id="demo-activity" hidden>Run demo activity</button>
            <button type="button" id="open-test-tab" hidden>Open test tab</button>
          </div>
          <div class="help-content split">
            <div class="help-filter-panel" id="help-filter-panel" aria-hidden="true">
              <div class="help-filter-icon-strip">
                <button type="button" id="help-filter-all" class="help-filter-chip" data-help-filter="all" aria-pressed="true" aria-label="All"><i class="icon fa-sharp-duotone fa-solid fa-heart" aria-hidden="true"></i></button>
                <button type="button" id="help-filter-pages" class="help-filter-chip" data-help-filter="pages" aria-pressed="false" aria-label="Pages"><i class="icon fa-sharp-duotone fa-solid fa-star" aria-hidden="true"></i></button>
                <button type="button" id="help-filter-dev" class="help-filter-chip" data-help-filter="dev" aria-pressed="false" aria-label="Dev"><i class="icon fa-sharp-duotone fa-solid fa-flag" aria-hidden="true"></i></button>
                <button type="button" id="help-filter-settings" class="help-filter-chip" data-help-filter="settings" aria-pressed="false" aria-label="Settings"><i class="icon fa-sharp-duotone fa-solid fa-bookmark" aria-hidden="true"></i></button>
                <button type="button" id="help-filter-mail" class="help-filter-chip" data-help-filter="mail" aria-pressed="false" aria-label="Mail"><i class="icon fa-sharp-duotone fa-solid fa-tag" aria-hidden="true"></i></button>
              </div>
            </div>
            <div class="pane pane-list help-index" id="help-index"></div>
            <button type="button" class="split-resize" data-split="help" aria-label="Resize help panes"></button>
            <div class="pane pane-detail help-article" id="help-article" tabindex="-1"></div>
          </div>
           <div class="button-bar pav-module-footer help-toolbar-bottom" id="help-toolbar-bottom" role="toolbar" aria-label="Help actions">
          </div>
        </div>
      </div>
    </div>`;
