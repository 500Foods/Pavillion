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
          `This copy is Pavillion ${APP_VERSION}. The window title shows the same number.`,
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
          "The list on the left is the index. The page on the right is the topic. Drag the bar between them to change the width. The width is saved. Search in the bar matches a title or the words on a page, and Enter opens the first hit.",
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
          "Mail will be the message list beside the open message. Nothing is connected yet, so the list is empty. Ctrl+1 opens Mail and shows its nav, unless Features gave that number to another button.",
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
          "Features is the first Settings page. The others are Themes, Icons, Fonts, Tabs, and Zooms. Each of those pages has a help icon on the right of its top bar. The icon opens that topic here.",
          "The page lists every rail button: Mail, Contacts, Calendars, Tasks, Files, Notes, Argent, Grok Bots, GitHub, Activity, Settings, Update, and Help. Each row shows that button's icon. Drag a row by the bars on its left to change its place. Dragging an icon on the top or bottom bar does the same move: left or right stays in that bar, and crossing to the other bar moves it. A short press on the bar still opens the section. Rows under Top sit on the top bar. Rows under Bottom sit on the bottom bar. A check shows that button. Turning one off hides it and closes its home tab. A hidden button stays in the list and keeps its place, and it does not take a cell on the rail. Settings has no check, so it stays available. Update has no home tab. Mail, Contacts, Calendars, and Files start on. Activity, Update, and Help start on and can be turned off. Tasks, Notes, Argent, Grok Bots, and GitHub start off. The choice is saved and comes back after a restart.",
          "Each row has a shortcut, Ctrl and a number from 1 to 9, or None. A number belongs to one row. Giving it to another row clears it from the first. Mail starts at Ctrl+1, Contacts at Ctrl+2, Calendars at Ctrl+3, Tasks at Ctrl+4, Files at Ctrl+5, Notes at Ctrl+6, and Argent at Ctrl+7. The other rows start with no shortcut. A hidden button's shortcut does nothing. Moving a button does not move its shortcut. A shortcut on Update runs a sync.",
          "If the section in the nav is hidden when the app opens, the nav opens on the first section above the line that is on and has its own nav. If none of those are on, it opens on Settings. Grok Bots, GitHub, Activity, and Update do not take that place. Settings can sit above the line and take it.",
        ],
      },
      {
        id: "zoom",
        title: "Zooms",
        paragraphs: [
          "Shell is the master scale, from 50% to 200% in steps of 5%. It multiplies every other row. Those rows run from 5% to 500% in the same steps. Button bars are the nav, the tabs, the log bars, the Help search bar, and the bars on settings pages. Help index and Help content are their own rows.",
          "Hold Ctrl and roll the wheel over a button bar, the nav, the calendar, a mail pane, the log text, the Help index, or the Help page. One notch changes that row by 5%. The wheel does not change Shell. A plain wheel still scrolls.",
          "Away zooms in unless the switch on this page says toward. The switch is for a touchpad whose roll feels backwards. The choice is saved with the other zoom settings. The Help index and the Help page take their colors from the theme.",
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
          "Update asks for a sync and spins while sync work is running. Until accounts exist, that request does nothing. Settings → Features can hide Update, and can give it a shortcut that runs the same sync. Activity can be hidden there too. A release build hides the demo item on the Help bar.",
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
        id: "index",
        title: "Pavillion.json",
        paragraphs: [
          "The working directory holds Pavillion.json, a profiles folder, a logs folder, the database file, blobs, and cache. Pavillion.json is the index. It is never a profile. The app writes it on first run and keeps unknown keys on a later rewrite.",
          "A profile is a .json file in the profile folder, other than the index, with kind pavillion-profile. The id must match the file name without .json. A mismatch is skipped. Anything else in that folder is ignored and logged.",
        ],
        code: `{
  "kind": "pavillion-index",
  "schema": 1,
  "active": "default.json",
  "profile_dir": "profiles",
  "log_level": "info"
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
  "logs": "logs/default.log",
  "blobs": "blobs/default",
  "cache": "cache/default",
  "accounts": [],
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
        id: "store",
        title: "The store",
        paragraphs: [
          "Opening a profile migrates the database before the activity list is loaded. A fresh file is user_version 1. The tables are activity, account_state, remote_account, identity, mailbox, sync_state, and kv. An activity table already in the file is kept. A database newer than this binary does not open.",
          "One writer thread owns the only write connection. Reads use two read-only connections. Finished activities are queued on that writer and are not written on the core thread. The page does not talk to the database.",
        ],
      },
      {
        id: "api",
        title: "How the window talks to the core",
        paragraphs: [
          "The API version is 3. A mismatch prints to stderr and exits before the window. Commands and events cross one forwarding call. The Rust shell does the Tauri work and does not grow new commands. WebKit stays the system library. It is not copied into the executable.",
          "The app version is separate. It is 0.phase.build. The phase stamp is 0.phase.0 in Cargo.toml and tauri.conf.json. The build count is the PAVILLION_BUILD file, and the shell and the page read it. make dev, make build, and make mini add one before they compile. Accepting a phase moves the middle number and leaves the build count in place. make test does not add one.",
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
