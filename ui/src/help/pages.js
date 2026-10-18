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
          "The middle number moves when a phase is accepted. The last number counts builds. make dev, make build, and make mini add one before they compile. Accepting a phase leaves that count in place.",
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
          "Pavillion is one Linux desktop for mail, contacts, calendars, and files. It is aimed at a self-hosted Stalwart server, with IMAP mail later. It takes the place of Betterbird, CardBook, and OmaCal.",
          "One profile is open for the life of the process. Switching to another profile is a restart. The window talks to a C core. The core keeps the mail, the store, and the secrets.",
        ],
      },
      {
        id: "reader",
        title: "How to use Help",
        paragraphs: [
          "The list on the left is the index. The page on the right is the topic. Drag the bar between them to change the width. The width is saved. Search in the bar matches a title or the words on a page, and Enter opens the first hit.",
          "Escape clears the search and leaves the page you were reading. A match on a page title keeps every section of that page in the index. A match in the text keeps only those sections in the index, and the page still shows the whole topic.",
          "Pages are added as features land. A short page means that part of the app is not built yet. A help icon beside a control, when one exists, will open one section of a page and link back here.",
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
          "Mail will be the message list beside the open message. Nothing is connected yet, so the list is empty. Ctrl+1 opens Mail and shows its nav.",
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
          "Contacts will be a rolodex list beside the open contact. The nav has a letter strip, a search field, then accounts and recents. Those lists are empty. Ctrl+2 opens Contacts.",
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
          "The workspace shows a month. The nav shows a smaller month and a calendar list. In this preview the week starts on Monday, and the profile's week-start setting is not read yet. Ctrl+3 opens Calendars.",
          "Clicking a date does not take tab shortcuts away. Ctrl+W, Ctrl+Tab, and Ctrl+1 still work after that click.",
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
          "Files will be a folder list beside a preview. There are no folders yet. Ctrl+4 opens Files.",
        ],
      },
    ],
  },
  {
    id: "settings",
    title: "Settings",
    sections: [
      {
        id: "zoom",
        title: "Zoom",
        paragraphs: [
          "Shell is the master scale, from 50% to 200% in steps of 5%. It multiplies every other row. Those rows run from 5% to 500% in the same steps. Button bars are the nav, the tabs, the log bars, the Help search bar, and the bars on settings pages. Help index and Help content are their own rows.",
          "Hold Ctrl and roll the wheel over a button bar, the nav, the calendar, a mail pane, the log text, the Help index, or the Help page. One notch changes that row by 5%. The wheel does not change Shell. A plain wheel still scrolls.",
          "Away zooms in unless the switch on this page says toward. The switch is for a touchpad whose roll feels backwards. The choice is saved with the other zoom settings. The Help index and the Help page take their colors from the theme.",
        ],
      },
      {
        id: "theme",
        title: "Theme",
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
          "The window title is the app version. Help lists that number before Introduction. The middle number moves when a phase is accepted. The last number counts builds, and a phase acceptance leaves it in place. make dev, make build, and make mini add one before they compile.",
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
          "Each open view is a tab. A nav button opens that section's home tab, or brings it forward, and switches the nav. A single click on a tab shows it and leaves the nav where it is. A double-click brings that section's home tab forward.",
          "Ctrl+W or Ctrl+F4 closes the active tab. The close button and a middle-click do the same, including on a home tab. The tab on the left becomes active. Closing the last tab leaves the workspace empty. An unsaved tab asks first, and Cancel stops a batch close.",
        ],
      },
      {
        id: "shortcuts",
        title: "Shortcuts",
        paragraphs: [
          "Ctrl+Tab and Ctrl+Shift+Tab move along the strip. Alt+1 through Alt+9 open that position. A missing position does nothing. Ctrl+Shift+T reopens one of the last 20 closed tabs, with its pin and mute. Ctrl+R reloads the view and does not reload the window. Ctrl+T does nothing until browsing tabs exist.",
          "Ctrl+1, Ctrl+2, Ctrl+3, and Ctrl+4 open Mail, Contacts, Calendars, and Files. None of these run while a text field, a dialog, or a menu has focus. They do run when focus has dropped to the page, including after a click on a calendar date.",
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
          "An empty strip stays empty after a clean quit. The first launch, with nothing saved, opens Mail.",
          "Settings → Tabs has Restore tabs on launch. Turn it off and the next launch opens nothing. Click a nav button to open that section. An unexpected shutdown does the same and shows a notice: Unexpected shutdown, performing quick start.",
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
          "Update asks for a sync and spins while sync work is running. Until accounts exist, that request does nothing. A release build hides the demo item on the Help bar.",
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
          "preferences.ui is opaque to the core. The page stores zoom, nav width, splits, fonts, icons, the activity view, and the open tabs there. The core does not invent missing zoom keys and does not read the tab list. The page clamps the keys it knows and keeps the rest. log_level lives on the index because logging starts before a profile is usable. themes_folder is an optional index key. Absent, theme files sit beside Pavillion.json.",
          "The open tabs are preferences.ui.session.tabs. preferences.ui.session.restore defaults to on. preferences.ui.session.shutdown is clean after a quit and open while the app is running. The window stays up until that clean value is stored. preferences.ui.session.maxTabs defaults to 50. Zoom, the nav width, and the theme stay in their own keys.",
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
          "The POST body is one object: requestId, kind (hello, task, or prod), and for a task a text plus an optional context of type, id, and account number. type is message, contact, event, or file. The body does not include a message body, headers, or a file's bytes. HTTP 200 means the bot accepted the run. The finished work comes back through the command.",
          "The bot acks first, then does the task, then done or fail. Hello is answered with bot hello, including a name, an icon, and a color. A prod is answered with bot progress. A task that came from a trigger is answered the same way as one that was typed. Context ids are fetched with the command. They are not expected in the webhook body.",
          "Two secrets stay out of the profile and the log. The webhook URL and its key let Pavillion wake the bot, and they live in the keyring. The CLI key is stored as a hash and lets the bot act here. A quiet period defaults to 2 minutes. A sent request that is still quiet becomes noReply. A working request is not failed by that timer.",
          "The note you paste to a bot is added to Help when that phase lands. This page is the mechanics, not that note.",
        ],
      },
    ],
  },
];
