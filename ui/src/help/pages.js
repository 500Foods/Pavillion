/* Help topics. A page is one row in the index. A section is the snippet a later help icon can open.
   Text is shown as text. Nothing here is read from the working directory. */

export const helpPages = [
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
          "The list on the left is the index. The page on the right is the topic. Search in the bar matches a title or the words on a page, and Enter opens the first hit.",
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
          "Shell is the master scale, from 50% to 200% in steps of 5%. It multiplies every other row. Those rows run from 5% to 500% in the same steps. Button bars are the nav, the tabs, the log bars, and the bars on settings pages.",
          "Hold Ctrl and roll the wheel over a button bar, the nav, the calendar, a mail pane, or the log text. One notch changes that row by 5%. The wheel does not change Shell. A plain wheel still scrolls.",
          "Away zooms in unless the switch on this page says toward. The switch is for a touchpad whose roll feels backwards. The choice is saved with the other zoom settings.",
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
          "Pins last until you quit. A restart does not bring them back yet. Close others, Close to the right, and Close all sub-tabs leave pinned tabs open.",
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
          "preferences.ui is opaque to the core. The page stores zoom, nav width, splits, fonts, icons, and the activity view there. The core does not invent missing zoom keys. The page clamps the keys it knows and keeps the rest. log_level lives on the index because logging starts before a profile is usable. themes_folder is an optional index key. Absent, theme files sit beside Pavillion.json.",
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
        id: "api",
        title: "How the window talks to the core",
        paragraphs: [
          "The API version is 3. A mismatch prints to stderr and exits before the window. Commands and events cross one forwarding call. The Rust shell does the Tauri work and does not grow new commands. WebKit stays the system library. It is not copied into the executable.",
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
