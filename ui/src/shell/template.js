// html-ok: static shell markup. No outside data is interpolated.
export const shellMarkup = `<div class="shell">
      <nav class="nav" id="nav" aria-label="Sections">
        <div class="nav-buttons button-bar" role="toolbar" aria-label="Sections">
          <button type="button" data-section="mail" aria-pressed="true" aria-label="Mail">
            <img class="nav-mark" alt="">
            <i class="icon fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270" data-icon-definition="fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="contacts" aria-pressed="false" aria-label="Contacts">
            <i class="icon fa-sharp-duotone fa-solid fa-address-book" data-icon-definition="fa-sharp-duotone fa-solid fa-address-book" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="calendars" aria-pressed="false" aria-label="Calendars">
            <i class="icon fa-sharp-duotone fa-solid fa-calendar-days" data-icon-definition="fa-sharp-duotone fa-solid fa-calendar-days" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="files" aria-pressed="false" aria-label="Files">
            <i class="icon fa-sharp-duotone fa-solid fa-folder" data-icon-definition="fa-sharp-duotone fa-solid fa-folder" aria-hidden="true"></i>
          </button>
        </div>
        <div class="nav-body">
          <div data-nav="mail">
            <ul class="tree">
              <li><button type="button" aria-current="true">Inbox</button></li>
              <li><button type="button">Drafts</button></li>
              <li><button type="button">Sent</button></li>
              <li><button type="button">Archive</button></li>
            </ul>
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
          <div data-nav="files" hidden>
            <p class="empty">No folders.</p>
          </div>
          <div data-nav="settings" hidden>
            <div class="heading-list" role="list">
              <button type="button" data-heading="zoom" aria-current="true">Zoom</button>
              <button type="button" data-heading="theme">Theme</button>
              <button type="button" data-heading="fonts">Fonts</button>
              <button type="button" data-heading="icons">Icons</button>
            </div>
          </div>
        </div>
        <div class="nav-buttons button-bar" role="toolbar" aria-label="Tools">
          <button type="button" data-section="activity" aria-pressed="false" aria-label="Activity">
            <i class="icon fa-sharp-duotone fa-solid fa-list-timeline" data-icon-definition="fa-sharp-duotone fa-solid fa-list-timeline" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="settings" aria-pressed="false" aria-label="Settings">
            <i class="icon fa-sharp-duotone fa-solid fa-gear" data-icon-definition="fa-sharp-duotone fa-solid fa-gear" aria-hidden="true"></i>
          </button>
          <button type="button" id="update" aria-label="Update">
            <i class="icon fa-sharp-duotone fa-solid fa-arrows-rotate" data-icon-definition="fa-sharp-duotone fa-solid fa-arrows-rotate" aria-hidden="true"></i>
          </button>
          <button type="button" data-section="help" aria-pressed="false" aria-label="Help">
            <i class="icon fa-sharp-duotone fa-solid fa-circle-question" data-icon-definition="fa-sharp-duotone fa-solid fa-circle-question" aria-hidden="true"></i>
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
        <div class="panel split" id="panel-files" role="tabpanel" hidden>
          <div class="pane pane-list"></div>
          <button type="button" class="split-resize" data-split="files" aria-label="Resize file panes"></button>
          <div class="pane pane-detail"></div>
        </div>
        <div class="panel settings" id="panel-settings" role="tabpanel" hidden>
          <div data-settings="zoom">
            <h2>Zoom</h2>
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
            </div>
            <p class="muted">Shell multiplies every row below. Button bars covers the navigation, the main tabs, the log bars, and the bars on other settings pages. Each row is its own key on the profile.</p>
            <p class="muted" id="profile-name"></p>
          </div>
          <div data-settings="theme" hidden>
            <div class="theme-head">
              <h2>Theme</h2>
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
            <h2>Fonts</h2>
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
          <div data-settings="icons" hidden>
            <h2>Icons</h2>
            <label class="font-row">Source
              <select id="icon-source">
                <option value="theme" selected>Theme</option>
                <option value="override">Override</option>
              </select>
            </label>
             <p class="muted">Theme reads each <code>--pav-icon-*</code> value. That value may be a Font Awesome class string or an <code>&lt;i class='...'&gt;</code> element. Override keeps the values below on this profile.</p>
             <label class="font-row kit-row">Font Awesome kit
               <textarea type="text" id="fa-kit-html" class="kit-input" rows="3" spellcheck="false">45801522ef</textarea>
             </label>
             <p class="muted">The kit is a Font Awesome kit id (e.g. <code>45801522ef</code>) loaded from <code>kit.fontawesome.com</code>, or raw HTML (e.g. a <code>&lt;script&gt;</code> tag). Changing it requires a page reload to take effect.</p>
             <form class="icon-form" id="icon-form">
               <table class="icon-table">
                 <thead>
                   <tr>
                     <th>Nav button</th>
                     <th>Preview</th>
                     <th>Icon HTML</th>
                   </tr>
                 </thead>
                 <tbody id="icon-list">
                   <tr data-icon-key="mail">
                     <td>Mail</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270" data-icon-key="mail"></i></td>
                     <td><textarea type="text" data-icon-key="mail" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270</textarea></td>
                   </tr>
                   <tr data-icon-key="contacts">
                     <td>Contacts</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-address-book" data-icon-key="contacts"></i></td>
                     <td><textarea type="text" data-icon-key="contacts" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-address-book</textarea></td>
                   </tr>
                   <tr data-icon-key="calendars">
                     <td>Calendars</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-calendar-days" data-icon-key="calendars"></i></td>
                     <td><textarea type="text" data-icon-key="calendars" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-calendar-days</textarea></td>
                   </tr>
                   <tr data-icon-key="files">
                     <td>Files</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-folder" data-icon-key="files"></i></td>
                     <td><textarea type="text" data-icon-key="files" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-folder</textarea></td>
                   </tr>
                   <tr data-icon-key="activity">
                     <td>Activity</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-list-timeline" data-icon-key="activity"></i></td>
                     <td><textarea type="text" data-icon-key="activity" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-list-timeline</textarea></td>
                   </tr>
                   <tr data-icon-key="settings">
                     <td>Settings</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-gear" data-icon-key="settings"></i></td>
                     <td><textarea type="text" data-icon-key="settings" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-gear</textarea></td>
                   </tr>
                   <tr data-icon-key="update">
                     <td>Update</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-arrows-rotate" data-icon-key="update"></i></td>
                     <td><textarea type="text" data-icon-key="update" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-arrows-rotate</textarea></td>
                   </tr>
                   <tr data-icon-key="help">
                     <td>Help</td>
                     <td class="icon-preview"><span class="icon-preview-scrim"></span><i class="icon fa-sharp-duotone fa-solid fa-circle-question" data-icon-key="help"></i></td>
                     <td><textarea type="text" data-icon-key="help" class="icon-input" rows="3" spellcheck="false">fa-sharp-duotone fa-solid fa-circle-question</textarea></td>
                   </tr>
                 </tbody>
               </table>
               <button type="button" id="icon-preview" disabled>Preview</button>
               <button type="submit" id="icon-reload" disabled>Reload &amp; Save</button>
             </form>
             <p class="muted">Icons are rendered from your registered Font Awesome kit. Use class prefixes like <code>fa-sharp-duotone fa-solid</code> for Sharp Duotone Solid, or <code>fa-solid</code> for classic Solid. Or paste raw HTML like an inline SVG.</p>
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
        <div class="panel settings" id="panel-help" role="tabpanel" hidden>
          <p class="empty">Help is not written yet.</p>
          <button type="button" id="isolation-test" hidden>Show isolation test</button>
        </div>
      </div>
    </div>`;
