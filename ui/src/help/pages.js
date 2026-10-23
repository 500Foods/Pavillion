/* Help topics. A page is one row in the index. A section is the snippet a later help icon can open.
   Text is shown as text. Nothing here is read from the working directory. */

import { APP_VERSION } from "../version.js";

export const helpPages = [
  {
    id: "app",
    title: `Pavillion ${APP_VERSION}`,
    sections: [
      {
        id: "build",
        title: "This build",
        paragraphs: [
          `This copy is Pavillion ${APP_VERSION}. The window title shows the same number. Startup covers the window with that line for at least two seconds, longer if the restored tabs are not painted yet, then fades it out.`,
          "The middle number is how many plan steps are complete. It moves when a phase is accepted. The last number counts builds. make dev, make build, and make mini add one before they compile. Accepting a phase leaves that count in place.",
        ],
      },
    ],
  },
  {
    id: "introduction",
    title: "Introduction",
    sections: [
      {
        id: "about",
        title: "What Pavillion is",
        paragraphs: [
          "Pavillion is one Linux desktop for mail, contacts, calendars, tasks, files, and notes. It is aimed at a self-hosted Stalwart server, with IMAP mail later. It takes the place of Betterbird, CardBook, and OmaCal. Argent, the books, is an icon until that work is scoped. GitHub is an icon on the bottom rail until that work is scoped.",
          "The top of the rail starts as Mail, Contacts, Calendars, Tasks, Files, Notes, and Argent. Tasks, Notes, and Argent start hidden. The bottom starts as Grok Bots, then GitHub, both hidden, then Activity, Settings, Update, and Help. Drag an icon left or right, or from one bar to the other. A short press still opens that section. Settings → Features can move any of them above or below the line, and can hide any of them except Settings.",
          "One profile is open for the life of the process. Switching to another profile is a restart. The window talks to a C core. The core keeps the mail, the store, and the secrets.",
        ],
      },
      {
        id: "reader",
        title: "How to use Help",
        paragraphs: [
          "The list on the left is the index. The page on the right is the topic. Drag the bar between them to change the width. The width is saved. The bar above the page has the title, then search, then Back. Search matches a title or the words on a page, and Enter opens the first hit.",
          "Back returns to the tab that was open before Help, one step. It stays off when there is no such tab, or when that tab has been closed.",
          "Escape clears the search and leaves the page you were reading. A match on a page title keeps every section of that page in the index. A match in the text keeps only those sections in the index, and the page still shows the whole topic.",
          "Pages are added as features land. A short page means that part of the app is not built yet. A help icon on the right of a page's top bar opens that topic here.",
        ],
      },
    ],
  },
  {
    id: "mail",
    title: "Mail",
    sections: [
      {
        id: "layout",
        title: "The mail window",
        paragraphs: [
          "Mail opens as the nav beside an empty workspace. The nav is the mailbox tree from the server. Each account is a root, labeled with its number and its name, and its mailboxes sit under it. Inbox, Drafts, Sent, Archive, Junk, and Trash each have their own icon. Any other mailbox uses a folder icon. The unread badge is the count the server reported. Clicking a mailbox opens Mail with that mailbox as the context. Clicking an account expands or collapses it. The arrow on a mailbox that contains other mailboxes expands or collapses that branch. A selected row is marked. The tree rows are buttons so the keyboard works like other nav.",
          "The account root shows a thin bar in the account's color. While that account is syncing, the row shows a pie for that sync. The total is the sum of the folder counts, and the pie is the share of those messages stored so far. Its tooltip is the percent. Before the folder counts are known, the pie is empty and the tooltip says Syncing. A later folder count can move the percent down. Offline, a locked keyring, a rejected password, or a server error shows an error mark. Idle and disabled show no mark. A folder created in another mail client shows up here on the next sync, without restarting Pavillion. A count change updates that row in place.",
          "The tree engine keeps view state separate from the node model: expansion, selection, and hidden roots persist in preferences.ui.trees.mail across restarts. Collapsing a branch and reopening it restores its descendants exactly as they were. Keyboard shortcuts scoped to the tree use the arrow keys, Home/End, +/- to expand and collapse, and * to expand all siblings of the current branch.",
          "Rows show a primary unread badge, and a secondary badge that appears on hover for total messages or recursive folder size. Unread rows get a colored status marker, and a selected row stays selected on hover. Account nodes show a color swatch on the leading edge of the icon. A syncing row shows that pie, and errors are marked in red.",
          "Right-click a row, or press Shift+F10 or the Menu key, to open its menu. An account has Hide account, which removes that account from the tree and keeps it hidden. A mailbox can be marked favorite or infrequent. Collapse this branch and Properties are on every row. A toolbar above the tree has filter buttons: Star shows only favorite roots, Heart shows the second favorite set, and Ghost hides infrequent folders.",
          "Expanding and collapsing a branch animates the disclosure arrow and the child group together at the container level. The animation is interruptible: rapid toggling settles on the final state. If your system asks for reduced motion, these transitions are disabled and branches show or hide instantly.",
          "Selecting a mailbox lists that folder in the pane. The columns are From and Subject. From is the sender's name, or the email address when the name is empty. The list is the newest messages, up to the Message list cap on Settings → Limits. That cap starts at 10,000 and is a whole number from 1 to 10,000. The count above the list is how many messages are shown. When the folder is larger than the cap, the count is how many of the folder total are listed. A message added or removed in another mail program shows up after the next sync.",
          "When mail arrives, an HTML message is drawn in a sandboxed frame. Remote images stay off until you allow them for that message.",
        ],
      },
    ],
  },
  {
    id: "contacts",
    title: "Contacts",
    sections: [
      {
        id: "layout",
        title: "The contact window",
        paragraphs: [
          "Contacts will be a rolodex list beside the open contact. The nav has a letter strip, a search field, then accounts and recents. Those lists are empty. Ctrl+2 opens Contacts, unless Features gave that number to another button.",
        ],
      },
    ],
  },
  {
    id: "calendars",
    title: "Calendars",
    sections: [
      {
        id: "layout",
        title: "The calendar window",
        paragraphs: [
          "The workspace shows a month. The nav shows a smaller month and a calendar list. In this preview the week starts on Monday, and the profile's week-start setting is not read yet. Ctrl+3 opens Calendars, unless Features gave that number to another button.",
          "Clicking a date does not take tab shortcuts away. Ctrl+W, Ctrl+Tab, and Ctrl+1 still work after that click.",
        ],
      },
    ],
  },
  {
    id: "tasks",
    title: "Tasks",
    sections: [
      {
        id: "layout",
        title: "The tasks window",
        paragraphs: [
          "Tasks are not available yet. The button starts on the top rail, after Calendars, and it starts hidden. Settings → Features turns it on. By default, Ctrl+4 opens it when it is on, and does nothing when it is off.",
          "When the screen lands, a task is a CalDAV to-do. A task group is its own list. The calendar keeps events.",
        ],
      },
    ],
  },
  {
    id: "files",
    title: "Files",
    sections: [
      {
        id: "layout",
        title: "The files window",
        paragraphs: [
          "Files will be a folder list beside a preview. There are no folders yet. Ctrl+5 opens Files, unless Features gave that number to another button. Settings → Features can hide this button.",
        ],
      },
    ],
  },
  {
    id: "notes",
    title: "Notes",
    sections: [
      {
        id: "layout",
        title: "The notes window",
        paragraphs: [
          "Notes are not available yet. The button starts on the top rail, after Files, and it starts hidden. Settings → Features turns it on. By default, Ctrl+6 opens it when it is on, and does nothing when it is off.",
          "When the screen lands, notes live under .pavillion/notes on one account's file store. Search for them runs on this machine.",
        ],
      },
    ],
  },
  {
    id: "argent",
    title: "Argent",
    sections: [
      {
        id: "layout",
        title: "The books",
        paragraphs: [
          "Argent is not available yet. The button starts on the top rail, after Notes, a stack of coins, and it starts hidden. Settings → Features turns it on. By default, Ctrl+7 opens it when it is on, and does nothing when it is off. The books themselves wait.",
        ],
      },
    ],
  },
  {
    id: "settings",
    title: "Settings",
    sections: [
      {
        id: "features",
        title: "Features",
        paragraphs: [
          "Features is a Settings page. Accounts, Themes, Icons, Fonts, Tabs, Zooms, Logs, and Limits are the others. Each of those pages has a help icon on the right of its top bar. The icon opens that topic here.",
          "The page lists every rail button: Mail, Contacts, Calendars, Tasks, Files, Notes, Argent, Grok Bots, GitHub, Activity, Settings, Update, and Help. Each row shows that button's icon. Drag a row by the bars on its left to change its place. Dragging an icon on the top or bottom bar does the same move: left or right stays in that bar, and crossing to the other bar moves it. A short press on the bar still opens the section. Rows under Top sit on the top bar. Rows under Bottom sit on the bottom bar. A check shows that button. Turning one off hides it and closes its home tab. A hidden button stays in the list and keeps its place, and it does not take a cell on the rail. Settings has no check, so it stays available. Update has no home tab. Mail, Contacts, Calendars, and Files start on. Activity, Update, and Help start on and can be turned off. Tasks, Notes, Argent, Grok Bots, and GitHub start off. The choice is saved and comes back after a restart.",
          "Each row has a shortcut, Ctrl and a number from 1 to 9, or None. A number belongs to one row. Giving it to another row clears it from the first. Mail starts at Ctrl+1, Contacts at Ctrl+2, Calendars at Ctrl+3, Tasks at Ctrl+4, Files at Ctrl+5, Notes at Ctrl+6, and Argent at Ctrl+7. The other rows start with no shortcut. A hidden button's shortcut does nothing. Moving a button does not move its shortcut. A shortcut on Update runs a sync.",
          "If the section in the nav is hidden when the app opens, the nav opens on the first section above the line that is on and has its own nav. If none of those are on, it opens on Settings. Grok Bots, GitHub, Activity, and Update do not take that place. Settings can sit above the line and take it.",
        ],
      },
      {
        id: "zoom",
        title: "Zooms",
        paragraphs: [
          "Shell is the master scale, from 50% to 200% in steps of 5%. It multiplies every other row. Those rows run from 5% to 500% in the same steps. Button bars are the nav, the tabs, the log bars, the module toolbars and footers, and the bars on settings pages. Help index and Help content are their own rows.",
          "Hold Ctrl and roll the wheel over a button bar, the nav, the calendar, a mail pane, the log text, the Help index, or the Help page. One notch changes that row by 5%. The wheel does not change Shell. A plain wheel still scrolls.",
          "Away zooms in unless the switch on this page says toward. The switch is for a touchpad whose roll feels backwards. The choice is saved with the other zoom settings. The Help index and the Help page take their colors from the theme.",
        ],
      },
      {
        id: "log",
        title: "Logs",
        paragraphs: [
          "Every log line is kept in the profile's log database, beside its store, up to a size cap from 1 MB to 100 MB (10 MB unless you change it). When the database is over the cap, the oldest lines go first: at shutdown, and when you press Apply or Trim Now.",
          "Refresh shows the size, the number of records, and the oldest line's local date and time. Moving the slider shows an estimate of what a trim to that size would keep; nothing is deleted until you press a button. Apply saves the new cap and trims to it. Trim Now trims to the saved cap.",
        ],
      },
      {
        id: "limits",
        title: "Limits",
        paragraphs: [
          "Limits sets how much of this profile stays on disk. Database starts at 2048 MB. Document starts at 4096 MB. Each value is a whole number of megabytes from 1 to 1048576. Database is message text, contacts, and calendars. Document is attachments and files. Beside each one is how much is on disk now, and how much space is free on the volume that holds this storage. Under that, each account has two figures. The Total row adds each column on its own. Database and Document are not added together. Database is how many messages, contacts, and events are on the server, and their size. Document is how many attachments and files are on the server, and their size. A size appears after Check sizes has split mail into message text and attachments. A contact, calendar, or file size that was not reported is left out. While that runs, the page shows a progress bar and a Cancel button. Cancel keeps the last finished size for a store that did not complete. The page shows when a check last finished. Message list starts at 10,000. It is a whole number from 1 to 10,000, and it is how many messages a folder lists. It is not a disk size. Apply saves these limits. Refresh loads the saved values and the quick counts again, and drops an unsaved edit. Refresh during a check does not throw that result away.",
          "A finished sync removes the oldest opened message text and documents that put the profile over a limit. Drafts, flagged messages, and mail waiting to send stay. Contacts and calendars stay. The log size cap stays on Logs. A later disk budget is another row on this page.",
        ],
      },
      {
        id: "theme",
        title: "Themes",
        paragraphs: [
          "Light, Dark, and System are built in. System follows the desktop, and Dark is used when that scheme cannot be read. A theme file is plain CSS. Its row name is the Title inside the file.",
          "Default, Undo, and Redo on this page change the theme only. They leave zoom where it is.",
        ],
      },
      {
        id: "fonts",
        title: "Fonts",
        paragraphs: [
          "The theme supplies the body font and the monospace font. Override stores this profile's own families. Vanadium Sans is the intended body face, and the page falls back to the system font when it is not installed.",
        ],
      },
      {
        id: "icons",
        title: "Icons",
        paragraphs: [
          "Nav icons come from a Font Awesome kit, or from an override on this profile. A selected theme file can supply the icons. Settings → Icons Override puts the profile's icons back on Light, Dark, and System.",
        ],
      },
      {
        id: "version",
        title: "Version",
        paragraphs: [
          "The window title is the app version. Help lists that number before Introduction. The middle number is how many plan steps are complete, and it moves when a phase is accepted. The last number counts builds, and a phase acceptance leaves it in place. make dev, make build, and make mini add one before they compile.",
        ],
      },
      {
        id: "accounts",
        title: "Accounts",
        paragraphs: [
          "Settings → Accounts is the first Settings heading and a split panel. The account table is on the left, grouped by server (the session URL's host and the full URL; accounts without one are under (no server)), each group with its count. Click a column header to sort it ascending, descending, or back to the original order; rows sort within their server. The footer shows the number of accounts and how many have each feature, then First, Previous, Next, and Last buttons (Up and Down work too when the table has focus) and Export, which saves the list as CSV (number, name, server, features, email; never passwords). Each row shows the account number, the color swatch with its icon, the name, and a read-only box for Mail, Calendar, Contacts, and Files (on when the last successful Test found that service and the account syncs it; the result is saved with the account, and hovering a box shows when it was checked; Files has no sync switch yet). The Mail cell also shows J (JMAP); I (IMAP) and S (SMTP) are greyed until they exist. Click a row to load that account's form on the right. Adding an account takes a name, a JMAP session URL (https only), a username, a password, and a color. The session URL can be discovered from the email domain, or overridden by hand. Under Advanced, an optional CA file (an absolute path to a PEM bundle) is trusted for that account in addition to the system store, for internal or test servers.",
          "The bar above the table has Add, Clone, and Delete, then the column chooser, then search. Add clears the selection and opens a blank form. Clone needs a selected row. It copies that account except the username, the password, the number, and the id. The name gains \" (copy)\". The copy stays in the form until you add it, and the service boxes stay off until that copy's own Test. Delete needs a selected row. It asks first, then removes the account, its saved password, and its local data. Search matches the name, number, email, username, or server, and it ignores case. Escape clears it. A server group with no match is hidden. Refresh, First, Previous, Next, and Last walk only the rows still shown. Refresh spins once, unless motion is reduced.",
          "The Test button checks the credentials against the server before Save is enabled. A wrong password shows one error and does not retry. The Password field is cleared after submit, and the secret is never shown once stored in the keyring. The Credentials area shows whether a password is stored, not what it is.",
          "The form is grouped as Identity (display name, color, email), Server (kind, session URL, auth method, Advanced), Credentials (username, password), and Options (sync, fetch bodies, enabled), with Test, Add, Save, and Remove below. Beside Color is a short row of icons drawn on the account color; the chosen one is the account's icon (the envelope by default). Detect, beside the session URL, finds it from the email address: first the _jmap._tcp SRV record of the domain, then https://<domain>/.well-known/jmap, following redirects without sending the password. On a saved account the URL it finds is saved at once and Detect shows Saved; on a new account it stays in the form until Add. When Test is redirected to a different final URL, that URL replaces the session URL the same way (Session URL updated), and if the redirect changed host, Test runs once more against it so the password is really checked. The email follows the username while the username looks like an address. On a saved account the password can stay blank: Test then uses the saved password, and the field shows Saved in keyring.",
          "Each account gets a three-digit number (001 and up) that never changes and is never reused after removal. The auth method is Basic in v1; OpenID Connect comes later.",
        ],
      },
    ],
  },
  {
    id: "tabs",
    title: "Tabs",
    sections: [
      {
        id: "strip",
        title: "The strip",
        paragraphs: [
          "Each open view is a tab. A suite button or Settings opens that section's home tab, or brings it forward, and switches the nav. Grok Bots, GitHub, Activity, and Help open a home tab and leave the nav where it is. A single click on a tab shows it and leaves the nav where it is. A double-click brings that section's home tab forward, and leaves the nav alone for Grok Bots, GitHub, Activity, and Help.",
          "Ctrl+W or Ctrl+F4 closes the active tab. The close button and a middle-click do the same, including on a home tab. The tab on the left becomes active. Closing the last tab leaves the workspace empty. An unsaved tab asks first, and Cancel stops a batch close.",
        ],
      },
      {
        id: "shortcuts",
        title: "Shortcuts",
        paragraphs: [
          "Ctrl+Tab and Ctrl+Shift+Tab move along the strip. Alt+1 through Alt+9 open that position. A missing position does nothing. Ctrl+Shift+T reopens one of the last 20 closed tabs, with its pin and mute. Ctrl+R reloads the view and does not reload the window. Ctrl+T does nothing until browsing tabs exist.",
          "A feature shortcut is Ctrl and a number, set on Settings → Features. Mail starts at Ctrl+1 and Argent at Ctrl+7, and the rows between them take Ctrl+2 through Ctrl+6. Any row can take a number from 1 to 9, or None. One number is used once. A hidden button's shortcut does nothing, and moving a button does not move its number. A shortcut on Update runs a sync. None of these run while a text field, a dialog, or a menu has focus. They do run when focus has dropped to the page, including after a click on a calendar date.",
          "Pin, Duplicate, Mute, Close others, and Close to the right have no shortcut of their own. Focus the tab and press Shift+F10 or the Menu key.",
        ],
      },
      {
        id: "pins",
        title: "Pinned tabs",
        paragraphs: [
          "Pin sits in the tab menu. A pinned tab moves to the left group and shows its icon only. Unpin puts it at the left of the unpinned tabs. Dragging a tab does not cross that boundary. Home tabs cannot be pinned or duplicated.",
          "A pin is saved with the tab. After a restart it comes back in the pinned group. Close others, Close to the right, and Close all sub-tabs leave pinned tabs open.",
        ],
      },
      {
        id: "restore",
        title: "After a restart",
        paragraphs: [
          "Quit with tabs open and they come back in the same order, with the same pin, mute, and active tab. The active tab loads immediately. The others load when you show them. A tab whose item is gone is left out, and a notice names it.",
          "An empty strip stays empty after a clean quit. The first launch, with nothing saved, opens Mail. A section hidden in Features does not come back, and the first launch skips Mail when Mail is hidden.",
          "Settings → Tabs has Restore tabs on launch. Turn it off and the next launch opens nothing. Click a nav button to open that section. An unexpected shutdown does the same and shows a notice: Unexpected shutdown, performing quick start. Closing the window and Ctrl+C both finish the save. A forced kill does not.",
          "Max tabs defaults to 50. The page keeps it between 1 and 200. The strip will not open another sub-tab past that number. Home tabs still open. A restart keeps every home tab, then the active tab and the pins, and leaves the rest closed.",
        ],
      },
    ],
  },
  {
    id: "activity",
    title: "Activity",
    sections: [
      {
        id: "views",
        title: "Activity and Logs",
        paragraphs: [
          "Activity and Logs share one tab. The nav button opens whichever you used last. Logs are the live log, with severity, subsystem, and search. Activity is the list of work the app is doing. The column is labeled Action.",
          "The log shows a time range: the last 12 hours unless you pick 1, 3, 6, or 24 hours, 7 days, All, or Custom (a From and To date and time). The choice is kept. The session list shows This run, Last run, or All, and that choice is kept too. Scroll above the oldest line, or widen the range, and older lines are read from the profile's log database. Copy, Save, and Print use the same range, session, and filters.",
          "Times in the log are local 24-hour time in the profile time zone; hover a line to see its UTC stamp. Copy, Save, and Print use the UTC lines. Every level is kept, from ERROR down to TRACE; the view starts at INFO. When the same line repeats, it is shown once, followed by \"previous line repeated N times\". Passwords, tokens, Authorization headers, URL queries, and user:password in URLs never reach the log, and email addresses appear only at DEBUG and TRACE.",
          "Update asks for a sync. A blue pie behind the icon follows that sync. The percent is messages stored against the folder counts, and the tooltip shows that percent along with how many syncs are running. The wedge catches up to a new percent over half a second, including when a later count moves the percent down. A click with nothing running still fills from empty to full over half a second. Click Update again to stop the sync and hide the pie. Until accounts exist, that request does nothing. Settings → Features can hide Update, and can give it a shortcut that runs the same sync. Activity can be hidden there too. A release build hides the demo item on the Help bar.",
        ],
      },
    ],
  },
  {
    id: "bots",
    title: "Grok Bots",
    sections: [
      {
        id: "section",
        title: "The section",
        paragraphs: [
          "Grok Bots starts as the first button on the bottom rail, a Sharp Thin ghost. GitHub starts as the button beside it, the GitHub mark. Both stay hidden until Settings → Features turns them on. Both start with no Ctrl number, and Features can give either one a number. Opening either one shows its home tab and leaves the nav on whatever section you were in.",
          "The home tab is a button bar and an empty transcript. Add is disabled. A bot cannot be connected yet, and nothing on this page calls out.",
        ],
      },
    ],
  },
  {
    id: "github",
    title: "GitHub",
    sections: [
      {
        id: "section",
        title: "The section",
        paragraphs: [
          "GitHub is not available yet. The button starts on the bottom rail, beside Grok Bots, and it starts hidden. Settings → Features turns it on. It starts with no Ctrl number, and Features can give it one. Opening it shows the home tab and leaves the nav on whatever section you were in.",
          "Later, the screen can pull events from GitHub and associate them with projects in Tasks, and those with contacts. That work is not built.",
        ],
      },
    ],
  },
  {
    id: "developer",
    title: "Developer",
    sections: [
      {
        id: "logging",
        title: "Logging",
        paragraphs: [
          "Everything that logs goes through pav_log in the core: C, the Rust shell (subsystem shell), and the UI through log.client (user actions at INFO, navigation at DEBUG, at most 50 lines a second; console.warn and console.error are forwarded too). Startup writes DEBUG marks for the mail-nav reload (mailboxes changed, account.list, mailbox.list, tree render, ready) and for the Font Awesome kit. The kit script is inserted after the first paint. The shell does not wait on that load. The mail tree is built only when the Mail nav is showing, so a Contacts-only restore does not call mailbox.list. A splash covers the window until that restore has painted. bypass.test.js fails on any new direct stderr, println!, or console call; the allowed ones are --help, --version, the API version mismatch line, and pav_log's own stderr copy.",
          "Lines are stored in <profile id>.log.db next to the store (UTC milliseconds), batched about every 100 ms; an ERROR waits for its commit. The file is trimmed to preferences.log_max_mb at shutdown. log.query pages history; log.export writes UTC lines.",
          "pav_log redacts every line before it is stored: the rest of an Authorization header, Basic and Bearer credentials, URL query strings and fragments, user:password@ in URLs, the value after a secret-named key (password, secret, token, key), and email addresses at INFO and above. Identical consecutive lines collapse into one plus a \"previous line repeated N times\" line, flushed by a different line, 1 s of quiet, a flush, or shutdown. Callers still pass ids, never content.",
        ],
      },
      {
        id: "module-bars",
        title: "Module bars",
        paragraphs: [
          "A module is one section of a view, such as Accounts or Help. Its toolbar and footer come from moduleToolbar and moduleFooter in ui/src/common/module-bar.js. A new module uses those builders. The toolbar is the title, selection actions, the column chooser, search, then other actions, then the help jump. The footer's first button is Refresh. A list adds First, Previous, Next, and Last, and puts Export on the right. Both bars are button bars, so Button bars zoom applies. Icon-only controls use the shared tooltip, not a native title.",
        ],
      },
      {
        id: "sync",
        title: "Mail sync",
        paragraphs: [
          "An enabled JMAP account starts a mail sync when the core is ready. sync.now asks again for one account, or for every enabled account when the call leaves the account id out. The states are idle, syncing, offline, locked, error:auth, error:server, and disabled. sync.state carries the account id, the state, and a short detail.",
          "A 401 leaves the account at error:auth until the password changes. A locked keyring leaves it at locked until sync.now or the next poll. This build stores the Session, the remote mail accounts, identities, and mailboxes. mailbox.list returns the tree: one account root, then each mailbox. identity.list returns the synced identities. A mailbox change emits mail.mailboxes_changed. A mailbox created on the server is stored on the next poll, which is 2 minutes. The Mail nav reads mailbox.list and patches the open tree from that event. sync.state updates the account root.",
          "tests/support/record.sh stores a scrubbed Session, Mailbox/get, and Identity/get from the Stalwart container. The fixture serves one of those files when the method and the arguments match. Every other call still uses the scripted mailboxes.",
          "A Session account id is the object key, and mail is under accountCapabilities. The suite parses the recorded Session and requires account b to keep the mail capability. The fixture Session uses that same shape, so mailbox sync runs against it.",
          "Identity/get is sent with the submission capability. Stalwart rejects the method without it. A mail account that does not advertise submission syncs mailboxes only.",
          "Mailbox sync asks Mailbox/query for a page of ids, then Mailbox/get for those ids, and repeats until the server's total is stored. The page size is the Session's maxObjectsInGet, and at most 500. A stored tree that already has that many mailboxes is checked against the server total, and a larger total walks the list again.",
          "Header sync then asks Email/query for each mailbox, newest first, Inbox and Sent before the others. The first page is that same cap, and the rest of the mailbox follows in pages of that size. Email/get asks for header fields only, in the same request. Each page that stores messages emits mail.messages_changed. The activity total starts as the sum of the folder counts. The first page of a folder replaces that folder's count with the query total. The mail nav draws that progress as a pie on the account row. The Update button draws the same percent behind its icon and puts it in the tooltip. A second click cancels the running sync. Quitting wakes the transfer so an in-flight request is dropped instead of waiting out the poll.",
          "message.list takes {view:{kind:\"mailbox\", id}} where id is the mailbox tree node id. It returns accountId, total, and messages of id, from, and subject, newest received_at first, then email.rowid descending. The cap is preferences.cache.listRows. A further row that would push the reply past 2 MB is left out, and total stays the folder count. The mail list loads that one reply and loads it again on mail.messages_changed for that account. It does not ask for another page while scrolling.",
          "Once that first walk has stored an Email state, a later sync asks Email/changes and Thread/changes instead of walking every mailbox. The first of those syncs asks Thread/get for the threads already stored, saves that state, and leaves the messages in place. A later sync uses Thread/changes. Created and updated messages fetch the same headers. A destroyed message drops its row, its full-text row, its body files, and a blob file that no other message still uses. A flag, move, or delete made in another client shows up on the next poll, which is still 2 minutes. Folder totals come from Mailbox/get. The email sync does not add or subtract them. A changes pass reports activity detail Changes with no percent, so the account pie stays on Syncing. If the server cannot calculate changes, that type is dropped and synced again, and the log records a warning.",
          "Unity's sync waits allow 30 seconds. A page cap of 2 on the Windows fixture needs more round trips than 8 seconds, and the account is still syncing when that shorter wait ends.",
        ],
      },
      {
        id: "index",
        title: "Pavillion.json",
        paragraphs: [
          "The working directory holds Pavillion.json, a profiles folder, the database file, its log database (default.log.db), blobs, and cache. Pavillion.json is the index. It is never a profile. The app writes it on first run and keeps unknown keys on a later rewrite.",
          "A profile is a .json file in the profile folder, other than the index, with kind pavillion-profile. The id must match the file name without .json. A mismatch is skipped. Anything else in that folder is ignored and logged.",
        ],
        code: `{
  "kind": "pavillion-index",
  "schema": 1,
  "active": "default.json",
  "profile_dir": "profiles",
  "log_level": "info",
  "next_account_number": 1
}`,
      },
      {
        id: "profile",
        title: "The profile",
        paragraphs: [
          "One profile stays loaded until the process stops. Paths are relative to the working directory unless they begin with a slash. The engine is sqlite. The connection field is only for another engine, and it holds no password. Message bodies are files under blobs.",
          "preferences.ui.features holds mail, contacts, calendars, tasks, files, notes, argent, bots, github, activity, update, and help. Each is true or false. Settings is not in that list. A missing value is on for mail, contacts, calendars, files, activity, update, and help, and off for tasks, notes, argent, bots, and github. preferences.ui.rail is top and bottom, the button order. preferences.ui.chords maps a button to a digit from 1 to 9, or to nothing. The core does not read these keys.",
          "preferences.ui is opaque to the core. The page stores zoom, nav width, splits, fonts, icons, the activity view, the rail order, the shortcuts, and the open tabs there. The core does not invent missing zoom keys and does not read the tab list. The page clamps the keys it knows and keeps the rest. log_level lives on the index because logging starts before a profile is usable. themes_folder is an optional index key. Absent, theme files sit beside Pavillion.json.",
          "The open tabs are preferences.ui.session.tabs. preferences.ui.session.restore defaults to on. preferences.ui.session.shutdown is clean after a quit and open while the app is running. Ctrl+C requests that same quit. The window stays up until that clean value is stored. A forced kill leaves it open. preferences.ui.session.maxTabs defaults to 50. Zoom, the nav width, and the theme stay in their own keys.",
          "Ctrl+C is caught in the UI process. JavaScriptCore's scavenger thread does not keep the blocked mask, so the shell installs a handler that only counts the signal and then runs the same quit as the window close. WebKitGTK 2.52 aborts its web process during EGL teardown after that save. The shell sets WEBKIT_DISABLE_COMPOSITING_MODE when it is unset, which turns accelerated compositing off. A value already in the environment is left alone.",
        ],
         code: `{
  "kind": "pavillion-profile",
  "schema": 1,
  "id": "default",
  "name": "Default",
  "storage": { "engine": "sqlite", "path": "default.sqlite" },
  "logs": "default.log.db",
  "blobs": "blobs/default",
  "cache": "cache/default",
  "accounts": [
    {
      "id": "a-500foods-andrew",
      "number": "001",
      "name": "andrew@500foods.com",
      "kind": "jmap",
      "sessionUrl": "https://mail.500foods.com/.well-known/jmap",
      "username": "andrew@500foods.com",
      "auth": "basic",
      "color": "#2a7",
      "enabled": true,
      "syncMail": true,
      "syncContacts": true,
      "syncCalendars": true,
      "fetchBodies": "recent"
    }
  ],
  "preferences": {
    "timezone": "America/Vancouver",
    "week_start": "monday",
    "time_format": "24h",
    "theme": "system",
    "density": "comfortable"
  }
}`,
      },
      {
        id: "rail-drag",
        title: "Dragging the rail",
        paragraphs: [
          "A Features row drag listens for pointermove on the document. Pointer capture on the row is dropped as soon as the pointer leaves it, so a listener on the row never sees the move. The Font Awesome kit replaces the grip icon with an svg. The grip wraps that icon, and the row calls preventDefault on pointerdown, or the svg starts a native image drag. Drag surfaces set user-select to none.",
        ],
      },
      {
        id: "corpus",
        title: "Where Help lives",
        paragraphs: [
          "The pages are in the UI bundle, in ui/src/help/pages.js. Nothing is read from the working directory, and there is no separate help file beside Pavillion.json. The index is a plain list. When the shared tree engine exists, that list is replaced by the engine. The pages stay in this file. A section id is the hook a later help icon will open.",
        ],
      },
      {
        id: "tree",
        title: "The tree engine",
        paragraphs: [
          "The tree has four modules: a model (nodes by id, parent-child links, ordering), view state (expanded set, selected/focused, hidden roots, favorites, filter flags), a renderer (row templates, icons, badges, states, CSS classes), and a controller that ties them together. The renderer uses plain DOM, not a table library.",
          "Row regions are configurable per tree: leading icon, label, status, count (unread badge), hover (secondary metadata), and trailing actions. The order comes from the options.regions array. All outside data is set via textContent; no HTML is parsed.",
          "Incremental updates use patchRow and the controller's patchNode method to update one row's badge or state class without re-rendering siblings. A count change from the server patches the node in the model and calls patchRow on the existing DOM row.",
          "Context menus are supplied by the section as data: an array of { label, onSelect, disabled, current } items. The J-1 menu module binds per-row, and Shift+F10 or the Menu key opens the menu for the focused row. The tree exposes rowMenuProvider via setRowMenu so the section can return per-node items.",
          "Filter flags (showFavorites, showHeart, hideInfrequent) live in view state alongside expanded and hidden roots. The star/heart/ghost toolbar buttons toggle them, and renderTree re-evaluates root visibility on each filter toggle. These flags persist in preferences.ui.trees.<treeId>.",
        ],
      },
      {
        id: "help-theme",
        title: "Help colors",
        paragraphs: [
          "Help index is the zoom key helpIndex. Help content is helpContent. Both live on preferences.ui.zoom with the other pane scales. Shell multiplies them. A missing key is filled at 100% the next time the page saves. The Help search bar stays on the Button bars key.",
          "The bar between the index and the page is preferences.ui.help. split starts at 0.25 of the row. The drag stays between 0.15 and 0.6. A missing object is filled from those defaults the next time the page saves. The bar uses --pav-help-index-border.",
          "A theme file can restyle Help without touching the mail panes. The names are --pav-help-index-bg, --pav-help-index-text, --pav-help-index-muted, --pav-help-index-current, --pav-help-index-border, --pav-help-content-bg, --pav-help-content-text, --pav-help-content-heading, --pav-help-content-muted, --pav-help-code-bg, --pav-help-code-text, and --pav-help-code-border. Light, Dark, and both starter themes define each one. Code samples still use the theme monospace font.",
        ],
      },
      {
        id: "secrets",
        title: "Secrets",
        paragraphs: [
          "Account passwords are not in the profile, the database, or the log. They live in a Stronghold snapshot under the profile data directory. The operating-system keyring holds only the snapshot password, so unlocking the session opens the snapshot without a second prompt. A locked keyring is a locked account, not a retry loop.",
          "The credentials form clears its field after submit. A command result never includes the secret. The Linux check is tests/manual/secret_smoke.sh. secret-tool search service pavillion shows the snapshot-password entry and not the dummy secret.",
        ],
      },
      {
        id: "accounts",
        title: "Accounts and secrets",
        paragraphs: [
          "Accounts live in the active profile as a list of objects. Each has an id, a number (001 and up), a name, a kind (jmap or imap), a session URL (https only), a username, an auth method (basic in v1), a color, sync flags, and fetch_bodies (none, recent, or all). No password is a field.",
          "Secrets live in a Stronghold snapshot under the profile data directory. The operating-system keyring holds only the snapshot password, so unlocking the session opens the snapshot without a second prompt. A locked keyring is a locked account, not a retry loop.",
          "The account commands are account.add (writes the secret to the keyring first, then the profile, then inserts account_state, rolling back on failure), account.list, account.update, account.remove, and account.set_secret. account.remove deletes the keyring entry, the profile entry, and account_state, with an optional purgeLocal cascade. accounts.changed is emitted after any of them. account.test comes with M-2.",
          "account.test follows the session URL (or builds it from the username's domain via /.well-known/jmap) over HTTPS with curl, honoring a 302/301/307/308 Location redirect. It maps JMAP capability URNs to mail, submission, contacts, calendars, sieve, files, and websocket, and resolves the primary account per capability. The session is fetched on a background net thread per server.",
          "account.discover {email} finds a session URL with no credentials: SRV _jmap._tcp.<domain> (target and port, then /.well-known/jmap), then https://<domain>/.well-known/jmap, one attempt each. The final response must be JSON with an apiUrl, and the result is the final URL after redirects, with via srv or well-known. SRV goes through the platform table (srv_lookup; the fake answers from pav_platform_fake_set_srv). account.test takes an optional id and uses that account's saved secret when no secret is typed. Both fail with server when a 200 is a web page, and drop Authorization when a redirect changes scheme, host, or port. A refused second launch prints one stderr line and exits 0; the running window unminimizes, shows, and takes focus. Each account stores email ('' or an address; accounts saved before it get the username when that is an address). A session request that outlives its wait is cancelled and its late reply is dropped and logged at DEBUG. SRV uses libresolv on Linux and macOS and dnsapi (DnsQuery_A) on Windows.",
        ],
      },
      {
        id: "network",
        title: "Network and sync",
        paragraphs: [
          "The core makes HTTPS requests on one net thread per server, each owning a curl multi handle. Servers are keyed by scheme, host, and port. A stalled server never delays another because each endpoint has its own thread.",
          "Caps limit concurrency. Per server: 6 in flight. Per account: 2 in flight. An optional overall cap can be set with pav_net_set_overall_cap. New requests wake the multi loop with curl_multi_wakeup if available; otherwise the loop's timer and socket polling dispatch them.",
          "The retry policy: 429 and 503 honor a Retry-After header (seconds) when present, falling back to exponential backoff from 1s to 5 min with plus-or-minus 20% jitter. 401 and other 4xx are not retried. SSL verification failures and write errors are not retried.",
          "Streaming upload reads from an upload_path file; streaming download writes to a download_path file. The raw response body is freed on the net thread, and transfer logs are redacted: no Authorization header, no password, no token, no message body.",
        ],
      },
      {
        id: "store",
        title: "The store",
        paragraphs: [
          "Opening a profile migrates the database before the activity list is loaded. A fresh file is user_version 2. Schema v1 keeps activity, account_state, remote_account, identity, mailbox, sync_state, and kv. Schema v2 adds email, threads, body_cache, attachments, a contentless email_fts index, the outbox, and local drafts. An activity table already in the file is kept. A database newer than this binary does not open. Message bodies and attachments are files under the profile blobs directory, mode 0600. Cache limits default to 2048 MB of bodies and 4096 MB of blobs. Settings → Limits labels them Database and Document and writes the same keys. Message list is preferences.cache.listRows, default 10000, from 1 to 10000. message.list uses that cap. It is not a disk budget. Refresh there calls cache.usage for the bytes on disk, volumeFreeBytes, and, per account, the quick server counts for mail, contacts, calendars, and files. The page puts on-disk size and volumeFreeBytes on one line beside each cap, builds two account columns, and a Total row that sums each column on its own. Database entries are the mail, contact, and calendar counts. Database size adds mail.bodiesBytes and reported contact and calendar sizes. Document entries are mail.attachmentsCount plus the file count. Document size adds mail.attachmentsBytes and reported file sizes. A pool size stays blank until that mail split is known, and so does the Document entry count. Check sizes passes measure and walks Email and FileNode sizes the quota does not split, several windows per request and at most 4 for mail. The mail get also sums decoded text and HTML, and decoded attachments, and counts attachment parts, stored as mailBodiesBytes, mailAttachmentsBytes, and mailAttachmentsCount. cache.change reports progress. Cancel is cache.usage with cancel true and stops the walk. The last finished walk stays in kv limits.server. Refresh during a check does not drop the result. A blank cell was not reported. The page does not show serverBytes. IMAP, CalDAV, and CardDAV use the same fields later and stay blank in this phase. An over-limit pass after sync is the activity Evict cache.",
          "One writer thread owns the only write connection. Reads use two read-only connections. Finished activities are queued on that writer and are not written on the core thread. The page does not talk to the database. Each account still listed in the profile has an account_state row before sync starts. A missing row is inserted as idle, so a new database can sync the accounts the profile already has. Message rows come back only when that sync finishes. A committed write is checkpointed into the database file so a reader sees the new rows. When the local mail is empty and a folder reports messages, the message download runs again.",
        ],
      },
      {
        id: "api",
        title: "How the window talks to the core",
        paragraphs: [
          "The API version is 3. A mismatch prints to stderr and exits before the window. Commands and events cross one forwarding call. The Rust shell does the Tauri work and does not grow new commands. WebKit stays the system library. It is not copied into the executable.",
          "The app version is separate. It is 0.phase.build. The phase stamp is 0.phase.0 in Cargo.toml and tauri.conf.json. The build count is the PAVILLION_BUILD file, and the shell and the page read it. make dev, make build, and make mini add one before they compile. The middle number is the number of Done rows in the plan; marking a row Done moves it and leaves the build count in place. make test does not add one.",
        ],
      },
      {
        id: "tests",
        title: "Test infrastructure",
        paragraphs: [
          "make test runs Unity (build/core_test) then Vitest (npm --prefix ui test). The Unity test binary links the core library, jansson, SQLite, libsecret, and Stronghold. Vitest runs src/**/*.test.js in node.",
          "The fixture server is tests/support/jmapfix.sh, a Bash HTTPS server (socat for TLS, openssl for the test CA, jq for JSON) that serves a JMAP Session at /jmap/session, a /.well-known/jmap redirect, and a JMAP API at /jmap/api. It generates a test CA into build/test-ca/ and writes build/fixture-state.txt (port, CA path). make test starts it, runs Unity with PAV_FIXTURE_URL and PAV_FIXTURE_CA, then stops it. socat forks one handler per connection, so concurrent slow-response tests do not serialize. The handler reads the Content-Length body before it replies. An unread body makes the close a reset, and on Windows curl then reports a partial file with no bytes.",
          "make it-up writes the self-signed test certificate to build/stalwart-tls/ (tests/support/testca.sh, shared with the fixture server) and starts tests/stalwart/compose.yaml (Stalwart v0.16.26 pinned), which mounts it read-only. Accounts added against the container set CA file to build/stalwart-tls/test-ca.crt. make it-seed seeds accounts and mailboxes. make it-down removes the container. make test-it runs integration tests with PAVILLION_IT=1 and is skipped until M-2 (account.test) exists.",
        ],
      },
      {
        id: "bots",
        title: "Grok Bots",
        paragraphs: [
          "Grok Bots are not in the app yet. This is the contract the later phases follow.",
          "Pavillion calls the bot with one POST. The bot does not dial Pavillion. It answers with the pavillion command. The token is the PAVILLION_TOKEN environment variable and is never a command argument. pavillion help --json is the command list, once that command exists. JSON carries only the fields that command needs.",
          "The POST body is one object: requestId, kind (hello, task, or prod), and for a task a text plus an optional context of type, id, and account number. type is message, contact, event, or file. task and note are added when those screens exist. The body does not include a message body, headers, or a file's bytes. HTTP 200 means the bot accepted the run. The finished work comes back through the command.",
          "The bot acks first, then does the task, then done or fail. Hello is answered with bot hello, including a name, an icon, and a color. A prod is answered with bot progress. A task that came from a trigger is answered the same way as one that was typed. Context ids are fetched with the command. They are not expected in the webhook body.",
          "Two secrets stay out of the profile and the log. The webhook URL and its key let Pavillion wake the bot, and they live in the keyring. The CLI key is stored as a hash and lets the bot act here. A quiet period defaults to 2 minutes. A sent request that is still quiet becomes noReply. A working request is not failed by that timer.",
          "The note you paste to a bot is added to Help when that phase lands. This page is the mechanics, not that note.",
        ],
      },
    ],
  },
];
