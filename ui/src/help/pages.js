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
        id: "licenses",
        title: "Licenses",
        paragraphs: [
          "Pavillion is MIT licensed. The C HTML engine (lexbor) is Apache-2.0 and lives under third_party/lexbor in the source tree.",
          "The Fedora package uses system libraries (WebKitGTK, GMime, curl, and others); those stay with the distribution. The Windows installer ships MinGW DLLs next to the app, including LGPL libraries such as GLib and GMime, loaded dynamically so they can be replaced. A THIRD_PARTY.md file lists them and where to get source; the MSI also installs that file beside the executable. The full text is in the repository and on the project site.",
        ],
      },
      {
        id: "reader",
        title: "How to use Help",
        paragraphs: [
          "The list on the left is the index. The page on the right is the topic. Drag the bar between them to change the width. The width is saved. The bar above the page has a chevron, then the title, then search, then Back. The search field reads 🔍 Search help until you type. Back's icon sits beside the word, with a line on its left and none on its right. Search matches a title or the words on a page, and Enter opens the first hit.",
          "The chevron to the left of the title toggles a filter panel. A 1px vertical separator sits to the right of the chevron. The chevron stays on Button bars. The panel uses Filters. Each chip keeps a square the size of its icon, so the icons sit one under the next and do not cover each other. The panel is as wide as the chevron when Filters and Button bars are the same scale, and then its right border lines up with the separator. When the panel opens it fades in and the index shifts right by the panel's width. Closing it shifts the index back. Index titles use the same horizontal padding as a toolbar button. The panel holds filter chips as icons only: All (heart), Pages (star), Dev (flag), Settings (bookmark), and Mail (tag). Hover or focus a chip to read its label. One chip is pressed at a time. All shows every topic. Pages shows the app and Introduction. Dev shows the Developer topics. Settings shows Settings and its sub-pages. Mail shows the Mail topics. Picking a chip narrows the index to that category. Pressing the chevron again slides the panel out, fades it away, and the chevron turns back.",
          "Back returns to the tab that was open before Help, one step. It stays off when there is no such tab, or when that tab has been closed.",
          "Escape clears the search and leaves the page you were reading. A match on a page title keeps every section of that page in the index. A match in the text keeps only those sections in the index, and the page still shows the whole topic.",
          "Pages are added as features land. A short page means that part of the app is not built yet. A help icon on the right of a page's top bar opens that topic here. Below the content row is a bottom toolbar that holds a Refresh button on the left, and Copy, Save, and Print on the right. Each of those three keeps its icon beside the word, and each has a line on its left. The same single pixel top border as other module footers divides the toolbar from the page.",
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
          "Mail opens as the nav beside an empty workspace. The nav is the mailbox tree from the server. Each account is a root, labeled with its number and its name, and its mailboxes sit under it. Inbox, Drafts, Sent, Archive, Junk, and Trash each have their own icon. Any other mailbox uses a folder icon. The unread badge counts local messages in that folder that are not seen, not drafts, and not marked deleted. A parent folder does not add its children. The account badge is the sum of its folders. Clicking a mailbox opens Mail with that mailbox as the context. Clicking an account expands or collapses it. The arrow on a mailbox that contains other mailboxes expands or collapses that branch. A selected row is marked. The tree rows are buttons so the keyboard works like other nav.",
          "The account root shows a thin bar in the account's color. While that account is syncing, the row shows a pie for that sync. The total is the sum of the folder counts, and the pie is the share of those messages stored so far. Its tooltip is the percent. Before the folder counts are known, the pie is empty and the tooltip says Syncing. A later folder count can move the percent down. Offline, a locked keyring, a rejected password, or a server error shows an error mark. Idle and disabled show no mark. A folder created in another mail client shows up here on the next sync, without restarting Pavillion. A count change updates that row in place.",
          "The tree engine keeps view state separate from the node model: expansion, selection, and hidden roots persist in preferences.ui.trees.mail across restarts. Collapsing a branch and reopening it restores its descendants exactly as they were. Keyboard shortcuts scoped to the tree use the arrow keys, Home/End, +/- to expand and collapse, and * to expand all siblings of the current branch.",
          "Rows show a primary unread badge, and a secondary badge that appears on hover for total messages or recursive folder size. Unread rows get a colored status marker, and a selected row stays selected on hover. Account nodes show a color swatch on the leading edge of the icon. A syncing row shows that pie, and errors are marked in red.",
          "Right-click a row, or press Shift+F10 or the Menu key, to open its menu. An account has Hide account, which removes that account from the tree and keeps it hidden. A mailbox can be marked favorite or infrequent. Collapse this branch and Properties are on every row. A toolbar above the tree has filter buttons: Star shows only favorite roots, Heart shows the second favorite set, and Ghost hides infrequent folders.",
          "Expanding and collapsing a branch animates the disclosure arrow and the child group together at the container level. The animation is interruptible: rapid toggling settles on the final state. If your system asks for reduced motion, these transitions are disabled and branches show or hide instantly.",
          "Selecting a mailbox lists that folder in the pane. A color bar shows the account color. The columns are From (To in Sent and Drafts), Subject, a short preview, the time from Settings → Formats, flags, and an attachment mark. From, Subject, and the preview share the width left in the pane, and they keep it when the split moves. The time, the flags, and the paperclip size to what they show. Drag a heading to move a column. Drag its edge to change the width; that change comes out of From, Subject, and the preview. How From, Subject, and the preview share that width is remembered. Opening that folder again selects the message you had selected and returns the list to the scroll position you left. From is the sender's name, or the email address when the name is empty. To is the first recipient the same way. The time is the time of day for a message from today, the short date in the same year, and the full date after that. Unread messages show a dot, and a flagged, answered, or forwarded message shows its mark. The list is the newest messages, up to the Message list cap on Settings → Limits. That cap starts at 10,000 and is a whole number from 1 to 10,000. The count above the list is how many messages are shown. When the folder is larger than the cap, the count is how many of the folder total are listed. A message marked deleted is left out of the list and out of that total. j and k move the selection one row. Page Up and Page Down move a page of rows. Home and End move to the first and last message. Enter keeps the selection in the list. The list keeps only the rows on screen, so a large folder still scrolls smoothly. Opening a message is not in this build. A message added or removed in another mail program shows up after the next sync.",
          "Settings → Mail holds extra CSS property names for HTML mail. The built-in list stays fixed. One name per line. Apply saves the list, and an empty list clears the extras. Remote images stay off until you allow them for a message. The message view that draws HTML is not in this build yet.",
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
          "Features is a Settings page. Accounts, Themes, Icons, Libraries, Assets, Fonts, Formats, Tabs, Zooms, Logs, Limits, and Mail are the others. Each of those pages has a help icon on the right of its top bar. The icon opens that topic here.",
          "The page lists every rail button: Mail, Contacts, Calendars, Tasks, Files, Notes, Argent, Grok Bots, GitHub, Activity, Settings, Update, and Help. Each row shows that button's icon. Drag a row by the bars on its left to change its place. Dragging an icon on the top or bottom bar does the same move: left or right stays in that bar, and crossing to the other bar moves it. A short press on the bar still opens the section. Rows under Top sit on the top bar. Rows under Bottom sit on the bottom bar. A check shows that button. Turning one off hides it and closes its home tab. A hidden button stays in the list and keeps its place, and it does not take a cell on the rail. Settings has no check, so it stays available. Update has no home tab. Mail, Contacts, Calendars, and Files start on. Activity, Update, and Help start on and can be turned off. Tasks, Notes, Argent, Grok Bots, and GitHub start off. The choice is saved and comes back after a restart.",
          "Each row has a shortcut, Ctrl and a number from 1 to 9, or None. A number belongs to one row. Giving it to another row clears it from the first. Mail starts at Ctrl+1, Contacts at Ctrl+2, Calendars at Ctrl+3, Tasks at Ctrl+4, Files at Ctrl+5, Notes at Ctrl+6, and Argent at Ctrl+7. The other rows start with no shortcut. A hidden button's shortcut does nothing. Moving a button does not move its shortcut. A shortcut on Update runs a sync.",
          "If the section in the nav is hidden when the app opens, the nav opens on the first section above the line that is on and has its own nav. If none of those are on, it opens on Settings. Grok Bots, GitHub, Activity, and Update do not take that place. Settings can sit above the line and take it.",
        ],
      },
      {
        id: "zoom",
        title: "Zooms",
        paragraphs: [
          "Shell is the master scale, from 50% to 200% in steps of 5%. It multiplies the rows under it, except Color picker, Icon Wizard, and Icon results. Those three run from 100% to 300% in steps of 5%, and each one stays the same wherever that control opens. Color picker starts at 200%. Icon Wizard and Icon results start at 100%. The other rows run from 5% to 500% in the same steps.",
          "Button bars are the nav, the tabs, the log bars, the module toolbars and footers, and the bars on settings pages. Tables is the list on the left of Accounts, Icons, Libraries, and Assets. Details is the form on the right of Accounts, Libraries, and Assets. The Icons editor uses Icon Wizard. Icon results is the search grid in that editor. Settings is the body of Features, Themes, Fonts, Formats, Tabs, Logs, Limits, Mail, and this Zooms page. Mail list and Mail detail stay their own rows. Help index and Help content stay their own rows. Filters is the icon strip on the left of Help, and later strips use that same row. The log text uses Log. The Logs page body uses Settings.",
          "Hold Ctrl and roll the wheel over a button bar, the nav, the calendar, a mail pane, a table, a detail form, a settings page, the log text, the Help index, the Help page, or a filter strip. One notch changes that row by 5%. The wheel does not change Shell. A wheel over the open color picker changes Color picker. A wheel over a toolbar in the Icon Wizard changes Button bars. A wheel over the rest of the Icon Wizard changes Icon Wizard. A wheel over the icons in its search grid changes Icon results. A plain wheel still scrolls.",
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
          "A finished sync removes the oldest opened message text and documents that put the profile over a limit. Drafts, flagged messages, and mail waiting to send stay. Contacts and calendars stay. The log size cap stays on Logs. Recent icons starts at 12. It is a whole number from 1 to 48, and it is how many Icon Wizard picks are kept. Apply saves it with the other limits. A smaller number drops the older picks. A later disk budget is another row on this page.",
        ],
      },
      {
        id: "mail",
        title: "Mail",
        paragraphs: [
          "Mail on Settings lists extra CSS properties for HTML messages. The built-in list stays fixed. Each extra name is one line: a lowercase letter, then lowercase letters, digits, or hyphens, at most 40 characters. position, behavior, and a name that starts with a hyphen are refused. At most 32 names. The same name may appear twice. Apply saves the list. An empty box clears it. Refresh loads the saved list and drops an unsaved edit.",
        ],
      },
      {
        id: "theme",
        title: "Themes",
        paragraphs: [
          "Light, Dark, and System are built in. System follows the desktop, and Dark is used when that scheme cannot be read. A theme file is plain CSS. Its row name is the Title inside the file. --pav-picker-swatches is the color picker's row: hex colors separated by spaces. The row shows as many as that list has, up to 24. Colors you add are saved on the profile and stay when the theme changes.",
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
        id: "formats",
        title: "Formats",
        paragraphs: [
          "Formats is the Settings page after Fonts. It sets how dates and times are shown. Stored times stay UTC milliseconds. The zone row writes the profile time zone, and the list is the IANA names this computer offers. An empty locale uses the computer's locale. A tag such as en-CA keeps English names, and fr-CA uses French names. Apply saves. An invalid string is refused and is not written.",
          "Each row is one kind: Date, Time, Time with seconds, Date and time, Message list, Weekday, Relative, and Duration. Each kind has a short description and a list of defaults. A default's label is what that pattern shows for the sample, then the pattern, and the pattern that matches the 12-hour or 24-hour default is marked default. Custom keeps any other pattern the token list allows. Relative is long, short, or narrow. The Now column is the current time, except Duration, which uses the sample length. The Sample column is 14 October 2026 at 15:04:05.006 in America/Vancouver (2026-10-14T22:04:05.006Z). Duration's sample is 3 hours, 4 minutes, and 5.006 seconds. Reset puts that row back to its default and does not save until Apply. A string that matches the default is left out of the save, so the 12-hour and 24-hour switch still changes Time, Time with seconds, and Date and time. A saved string that differs from that default wins.",
          "The 24-hour defaults are Date yyyy-MM-dd, Time HH:mm, Time with seconds HH:mm:ss.SSS, and Date and time yyyy-MM-dd HH:mm. The 12-hour defaults for those three time rows are h:mm a, h:mm:ss.SSS a, and yyyy-MM-dd h:mm a. Message list is MMM d. Weekday is EEEE. Relative is long. Duration is h:mm:ss. The message list shows the time string on the same local day, the message-list string in the same year, and the date string in another year.",
          "A format is letters outside quotes. A quote is a straight single quote, and two quotes inside a quote are one quote character. The string needs at least one token, and a quote must be closed. On the October sample, the date and time tokens are: yyyy 2026, yy 26, y 2026, MM 10, M 10 (January is 1), MMM Oct, MMMM October, MMMMM O, dd 14, d 14 (the 5th is 5), E 3, EEE Wed, EEEE Wednesday, EEEEE W, HH 15, H 15 (09:00 is 9), hh 03, h 3, mm 04, ss 05, SSS 006, S 6, a PM, Z -7, ZZ -07:00, ZZZ -0700, ZZZZ PDT, ZZZZZ Pacific Daylight Time, z America/Vancouver. For example, 'at' HH:mm is at 15:04.",
          "Duration tokens are y, yy, yyyy, M, MM, w, ww, d, dd, h, hh, m, mm, s, ss, S, SS, and SSS. The sample length with h:mm:ss is 3:04:05. The Luxon token table is at https://moment.github.io/luxon/#/formatting?id=table-of-tokens",
          "Changing a format or the zone updates the times already on screen. Copy, Save, and Print in the log use the display lines. Save writes those lines. The core command log.export still writes UTC lines.",
        ],
      },
      {
        id: "icons",
        title: "Icons",
        paragraphs: [
          "Settings → Icons lists every icon in the app, grouped by where it appears, such as Nav, Mail, and Mail list. The table is on the left. The column chooser is the wide button at the top left of the table. A check in Default means that icon has not been changed. The icon and its name are always shown. Icon Format is Font Awesome, Other Library, PNG, SVG, or Custom. Default, Icon Format, and the key can be hidden from that chooser. Drag a heading to move that column, and drag its edge to change the width. Default, the icon, and Icon Format remember their width. Name and Key use the spare width, and how they share it is remembered. The bar between the table and the wizard is remembered. The icon you had open comes back, and the list stays at the scroll position you left. Up and Down move one row. Page Up and Page Down move a page of rows. Home and End move to the first and last row. Search matches the name, the key, the set, or Icon Format. The footer walks the rows that are still shown.",
          "The Icon Wizard is the pane on the right. The preview sits at the top left. The first square is 128 pixels, in the current theme. Under it, the word Theme has a line above and below. The same icon is shown on dark, then the word Dark with a line above and below, then on light, then the word Light with the same lines. Those lines meet the left edge of the pane. The same space sits on the left and right of the icons as above and below, and the column is wider so the icon stays 128 pixels. A vertical line runs from the top of the pane to the footer and meets the toolbar lines. The first toolbar is the group in bold, then the name, the key, and the format. That line is vertically centered and uses the same left padding as the other toolbars. Close is on this toolbar only when the Wizard is its own window. Close, Check, and X are the same width as Help. Default stays pinned on the second toolbar, with a right edge. The roof mark and the recent icons scroll beside it. That strip hides its scrollbar, and a wheel or a drag scrolls it. The check accepts and the X cancels. Each has a line on its left, and neither has a line on its right. Those two icons are Check and X in the icon list, so they can be changed there. The third toolbar is labeled Icon Format, in bold and in the accent color. Its choices are Font Awesome, Other Library, PNG, SVG, and Custom. Selecting a row opens that format. The Mail icon is a PNG, so that row opens on PNG, and the picture stays on the PNG tab when you leave it and come back. The fourth toolbar changes with that choice. Font Awesome and Other Library have a search. The first search of a library loads its catalog when none is stored yet. Typing waits half a second, and a search that is still running finishes before the next one starts. There is no minimum length. Up to 80 of 200 matches are shown. Typing clock finds every icon whose name contains clock. A misspelled word does not match. Those icons are drawn with the family, style, color, and flags on that toolbar. When Font Awesome has loaded, Family lists every Pro+ family, including Graphite, Chisel, Etch, Jelly, Notdog, Slab, Thumbprint, Utility, and Whiteboard. When it has not, the families are Classic, Sharp, Duotone, Sharp Duotone, and Brands. Style follows the family. Flags is a checkbox list for size, rotate, flip, styling, fixed width, and animation. When at least one flag is on, the button reads Flags and the count. It does not show a count of zero. A smaller X sits to the left of the Flags line and clears every flag. Size, rotate, and flip keep a single check. Styling can be on together: Swap opacity exchanges the two duotone layers, Border draws a ring, and Inverse turns the icon white. You can resize that list, and the size is remembered. A flag keeps the icon you already chose. A size such as 2x or XL draws that much larger inside the same box. That box follows the zoom, so a larger glyph does not move the icons beside it. Icons from one family share a height, and a wide one keeps its own width. Family, Style, and Color use that same bold accent as Icon Format. Family and Style stay wide enough for their text, and they have a right edge as well as the left one. Flags has a right edge, and the clear X sits to the left of that edge. Family, Style, Library, and Material style open the same kind of list as Flags. Each is a plain button with a left edge, a right edge, and an arrow. The list uses the window background and text. A check marks the chosen row. The row is filled only while the pointer or the keyboard is on it. Flags has that same edge and arrow. The name sits 8 px under each icon. Each of those cells has a 10 px margin and 20 px of padding. Search sits to the right of that toolbar, after the vertical rule, in the same kind of field as Search icons. It reads 🔍 Search Font Awesome until you type. Search icons on the Icons page starts the same way. A help icon beside it opens Font Awesome help. That icon is the same one as the Help button in the main nav. Hold Ctrl and roll the wheel over that grid to zoom it. That zoom is Icon results, from 100% to 300% in steps of 5%. It starts at 100% and does not change the icon. An icon the kit can draw in the family and style you picked is shown that way. An icon the kit has only in another family or style is still shown, and choosing it switches Family and Style. An icon the kit cannot draw is left out. A catalog row whose family is brands is drawn with the brands class and does not take the family you picked. Choosing it selects Brands. They fill the rest of the pane. Click one to select it. PNG and SVG have Load and Save. Custom edits the HTML, and Preview refreshes the square. SVG and PNG store the file with the profile and save an image that points at that file. Accept writes that icon at once. Close and Cancel leave the saved icon. That asks only after you have changed the icon. Choosing another row asks the same way. Hold Ctrl and roll the wheel over a toolbar to change Button bars zoom. Hold Ctrl and roll the wheel over the rest of the Wizard to zoom it from 100% to 300%. It starts at 100%, one notch is 5%, and the Away or Toward switch applies. Import, Export, and Revert all sit on the footer. Each icon sits beside its word, and each button has a line on its left. Revert all clears every saved icon. Import and Export save and restore those changes as a JSON file. The roof mark is the shipped Mail icon, and the same picture is the window icon. Help draws the full mark above the title on the first page. The Mail tab and the nav use a small copy of it, so the silver edge stays a line at that size. Account icons still open this Wizard as its own window, and a click outside that window closes it the same way.",
          "A saved icon wins over a theme file, and a theme file wins over the shipped icon. The first time this Icons page loads, older saved icons are cleared once.",
        ],
      },
      {
        id: "fontawesome",
        title: "Font Awesome",
        paragraphs: [
          "Font Awesome tools sit on the fourth toolbar of the Icon Wizard, on the left. When the kit has loaded, Family lists every Pro+ family, including Graphite, Chisel, Etch, Jelly, Notdog, Slab, Thumbprint, Utility, and Whiteboard. When it has not, the families are Classic, Sharp, Duotone, Sharp Duotone, and Brands. Style is Solid, Regular, Light, or Thin. Some families also have Semibold. Brands has no style, so that field hides. Family, Style, and Color are bold and use the accent color. Family and Style stay as wide as their names and have a right edge. Flags has a right edge, and the clear X sits to the left of that edge. Family and Style are plain buttons with a left edge, a right edge, and an arrow, the same kind of list as Flags. The top square on the left shows the icon in the current theme, including a size such as 2x. Theme, Dark, and Light each have a line above and below, and those lines meet the left edge of the pane. The same space sits on the left and right of the icons as above and below. Dark and Light show that same icon.",
          "Flags is the checkbox list, with a left edge and an arrow. Size, Rotate, and Flip keep one check each. Clicking the checked row clears it. Styling, fixed width, and the animation rows can be on together. Swap opacity exchanges the duotone layers, which often looks better on a duotone icon. Border draws a ring around the icon. Inverse turns it white. The list uses the window background and text. A check marks the chosen row. The row is filled only while the pointer or the keyboard is on it. The list can be resized, and that size is remembered the next time it opens. A size draws that much larger in the square, in the results, and wherever that icon is placed. The box stays put.",
          "Color sets the icon color. Search is on the right of that toolbar, after the vertical rule, and it lines up with Search icons. The line between the field and the help icon is the help button's left edge, and the two help buttons are the same width. The wizard search stays 16rem wide. The embedded wizard has no outer margin. The field reads 🔍 Search Font Awesome until you type. The help icon is the same icon as Help in the main nav. A word matches an icon whose name, label, or keyword contains that word. Results are icons the loaded kit can draw. One in the family and style you picked is drawn that way. One the kit has only in another family or style is still shown, and choosing it switches Family and Style to that pair. A search row whose family is brands is drawn with the brands class and does not take the family you picked. Choosing it sets Family to Brands. A name the kit keeps in its brands set is drawn the same way. One the kit cannot draw is left out. The help icon to the right of search opens this topic.",
        ],
      },
      {
        id: "libraries",
        title: "Libraries",
        paragraphs: [
          "Settings → Libraries lists the icon families Pavillion ships. Font Awesome starts on. Material Symbols, Iconify, Bootstrap Icons, Phosphor, Tabler Icons, and Remix Icon start off. Rows in one family sit together. The column chooser is the wide button at the top left of the table. Drag a heading to move that column, and drag its edge to change the width. On, Group, and Style remember their width. Name and the address use the spare width, and how they share it is remembered. The bar between the table and the form is remembered. The library you had selected comes back, and the list stays at the scroll position you left. Up and Down move one row. Page Up and Page Down move a page of rows. Home and End move to the first and last row. Add starts another Font Awesome row, and Remove drops the selected one. The form on the right sets the name, the family, and the style. A Font Awesome row also takes a kit id, so you can keep more than one kit, each with its own style. The other families build their address from the family and the style. A default set of icons for each row comes later.",
          "Save writes the list. A change to the address or the on/off switch reloads the window. A name change does not. A Font Awesome style change does not, because the kit address stays the same. A Material or Phosphor style change does reload, because that address changes. Pavillion loads only these families. Another family is added to the app. If you want one that is not here, say so and it can be considered. An @ before the host is a password and is refused. An @ later in the address, as in the Material font axes, is kept.",
          "Turning Iconify on loads its script. Icons from Iconify do not draw yet. That mapping comes later. Material Symbols, Bootstrap Icons, Phosphor, Tabler Icons, and Remix Icon draw once they are on, when an icon uses their classes. The Icon Wizard still writes Font Awesome and Material.",
        ],
      },
      {
        id: "assets",
        title: "Assets",
        paragraphs: [
          "Settings → Assets lists icon files stored with this profile. They are grouped by kind: SVG, PNG, PNG original, and Fetched. The column chooser is the wide button at the top left of the table. Drag a heading to move that column, and drag its edge to change the width. Kind and Size remember their width. The id uses the spare width. Dragging the id changes the column beside it, and that width is remembered. The bar between the table and the file is remembered. The file you had selected comes back, and the list stays at the scroll position you left. Up and Down move one row. Page Up and Page Down move a page of rows. Home and End move to the first and last row. The pane on the right shows the file. Delete asks, then removes it. The list loads when you open this page. When the files are over 128 MB, the page says so.",
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
          "Settings → Accounts is the first Settings heading and a split panel. The account table is on the left, grouped by server (the session URL's host and the full URL; accounts without one are under (no server)), each group with its count. Click a column header to sort it ascending, descending, or back to the original order; rows sort within their server. The footer shows the number of accounts and how many have each feature, then First, Previous, Next, and Last buttons (Up and Down, Page Up and Page Down, Home, and End work too when the table has focus) and Export, which saves the list as CSV (number, name, server, features, email; never passwords). Each row shows the account number, the color swatch with its icon, the name, and a read-only box for Mail, Calendar, Contacts, and Files (on when the last successful Test found that service and the account syncs it; the result is saved with the account, and hovering a box shows when it was checked; Files has no sync switch yet). The Mail cell also shows J (JMAP); I (IMAP) and S (SMTP) are greyed until they exist. Click a row to load that account's form on the right. Adding an account takes a name, a JMAP session URL (https only), a username, a password, and a color. The session URL can be discovered from the email domain, or overridden by hand. Under Advanced, an optional CA file (an absolute path to a PEM bundle) is trusted for that account in addition to the system store, for internal or test servers.",
          "The bar above the table has Add, Clone, and Delete, then the column chooser, then search. Add clears the selection and opens a blank form. Clone needs a selected row. It copies that account except the username, the password, the number, and the id. The name gains \" (copy)\". The copy stays in the form until you add it, and the service boxes stay off until that copy's own Test. Delete needs a selected row. It asks first, then removes the account, its saved password, and its local data. Search matches the name, number, email, username, or server, and it ignores case. Escape clears it. A server group with no match is hidden. Refresh, First, Previous, Next, and Last walk only the rows still shown. Refresh spins once, unless motion is reduced. Drag a heading to move that column. Name uses the spare width. Dragging another column's edge widens or narrows Name, and the table stays the width of the pane. Dragging Name changes the column beside it, and that width is remembered. The account you had selected comes back, and the list stays at the scroll position you left. Up and Down move one row. Page Up and Page Down move a page of rows. Home and End move to the first and last row.",
          "The Test button checks the credentials against the server before Save is enabled. A wrong password shows one error and does not retry. The Password field is cleared after submit, and the secret is never shown once stored in the keyring. The Credentials area shows whether a password is stored, not what it is.",
          "The form is grouped as Identity (display name, color, email), Server (kind, session URL, auth method, Advanced), Credentials (username, password), and Options (sync, fetch bodies, enabled), with Test, Add, Save, and Remove below. Color is a square swatch and opens the shared picker only. Close is on the picker; a click outside also closes it, and Escape closes it and restores the previous color. Hold Ctrl and roll the wheel over the open picker to zoom it from 100% to 300%. It starts at 200%, one notch is 5%, and the Away or Toward switch applies. The swatch row is the theme's list, up to 24. Add, Remove, and Close are icons on one row at the bottom of the picker. Add saves the current color with the profile and is disabled when that color is already shown. Remove drops a color you added and is disabled otherwise. Hover a swatch to read its name when it has one: a CSS color name, or the theme's name for that color. The account stores hex. The icon button beside the swatch opens the Icon Wizard; the envelope is the default. Detect, beside the session URL, finds it from the email address: first the _jmap._tcp SRV record of the domain, then https://<domain>/.well-known/jmap, following redirects without sending the password. On a saved account the URL it finds is saved at once and Detect shows Saved; on a new account it stays in the form until Add. When Test is redirected to a different final URL, that URL replaces the session URL the same way (Session URL updated), and if the redirect changed host, Test runs once more against it so the password is really checked. The email follows the username while the username looks like an address. On a saved account the password can stay blank: Test then uses the saved password, and the field shows Saved in keyring.",
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
          "Times in the log follow Settings → Formats, the time-with-seconds row. The default is local 24-hour time with seconds and milliseconds in the profile time zone. Hover a line to see its UTC stamp. Copy, Save, and Print use those display lines. Save writes every matching line from the log database, oldest first. Every level is kept, from ERROR down to TRACE; the view starts at INFO. When the same line repeats, it is shown once, followed by \"previous line repeated N times\". Passwords, tokens, Authorization headers, URL queries, and user:password in URLs never reach the log, and email addresses appear only at DEBUG and TRACE.",
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
          "Everything that logs goes through pav_log in the core: C, the Rust shell (subsystem shell), and the UI through log.client (user actions at INFO, navigation at DEBUG, at most 50 lines a second; console.warn and console.error are forwarded too). Startup writes DEBUG marks for the mail-nav reload (mailboxes changed, account.list, mailbox.list, tree render, ready) and for the Font Awesome kit. The kit script is inserted after the first paint. The shell does not wait on that load. The mail tree is built only when the Mail nav is showing, so a Contacts-only restore does not call mailbox.list. A change for one account while that first list is still waiting on account.list stays with the full list, so the tree is not replaced by that account alone. The window stays hidden until that restore has laid out. Meanwhile the shell shows a native splash (not a webview): a black window where the mark fades in. When the page sends pav-app-ready the window is shown and the splash fades out. If the page never says so, the window is shown after 5 s. bypass.test.js fails on any new direct stderr, println!, or console call; the allowed ones are --help, --version, the API version mismatch line, and pav_log's own stderr copy.",
          "Lines are stored in <profile id>.log.db next to the store (UTC milliseconds), batched about every 100 ms; an ERROR waits for its commit. The file is trimmed to preferences.log_max_mb at shutdown. log.query pages history. The log view's Save writes display lines through export.write_text. The core command log.export still writes UTC lines. ui/src/formats.js is the only module that turns a stored instant into display text.",
          "pav_log redacts every line before it is stored: the rest of an Authorization header, Basic and Bearer credentials, URL query strings and fragments, user:password@ in URLs, the value after a secret-named key (password, secret, token, key), and email addresses at INFO and above. Identical consecutive lines collapse into one plus a \"previous line repeated N times\" line, flushed by a different line, 1 s of quiet, a flush, or shutdown. Callers still pass ids, never content.",
        ],
      },
      {
        id: "formats",
        title: "Formats",
        paragraphs: [
          "ui/src/formats.js is the only module that turns a stored instant into display text. Screens call formatInstant. They do not format a date themselves. The strings live in preferences.ui.formats. The core does not read them. A missing time string follows time_format. A saved string that differs from that default wins, and a string equal to the default is left out of the save. Luxon does not reject an unknown token, so the page allowlists the tokens the Formats topic lists. dateShort is the message-list rule: the time pattern on the same local day, the dateShort pattern in the same year, and the date pattern otherwise. Copy, Save, and Print in the log use the display line. Save writes that line through export.write_text. log.export still writes UTC lines.",
        ],
      },
      {
        id: "memory",
        title: "Memory",
        paragraphs: [
          "The core reports memory usage as a trace log line at launch and after each full sync. It logs current RSS, peak RSS, and the combined RSS and child count of child processes such as WebKit: core=N.N MB peak=N.N MB webkit=N.N MB (N children). On Linux this reads /proc/self/statm for current RSS (not the high-water-mark ru_maxrss, which never decreases), and sums the /proc/PID/statm of child processes. On macOS the fake platform is used for CI; the production build uses mach_task_basic_info for current RSS and getrusage for peak. Windows uses GetProcessMemoryInfo for both current and peak working set size.",
        ],
      },
      {
        id: "module-bars",
        title: "Module bars",
        paragraphs: [
          "A module is one section of a view, such as Accounts or Help. Its toolbar and footer come from moduleToolbar and moduleFooter in ui/src/common/module-bar.js. A new module uses those builders. The toolbar is the title, selection actions, the column chooser, search, then other actions, then the help jump. The footer's first button is Refresh. A list adds First, Previous, Next, and Last, and puts Export on the right. A bar button with the class pav-labeled-action keeps its icon in the line beside the word, and has a left border. Import, Export, Revert all, Help's Back, and Help's Copy, Save, and Print use that class. Both bars are button bars, so Button bars zoom applies. Icon-only controls use the shared tooltip, not a native title.",
        ],
      },
      {
        id: "table-fill",
        title: "Table columns",
        paragraphs: [
          "Every Tabulator list fills its pane. The height is the pane. The columns share the width left after the vertical scrollbar, and they keep that width when a split moves. A column with widthGrow takes the spare width and is not given a fixed width. Mail grows From (weight 2), Subject (weight 3), and the preview (weight 2). Accounts grows Name. Icons grows Name and Key. Libraries grows Name and the address. Assets grows the id. A table that names no widthGrow column grows its last movable column.",
          "The other columns size to the heading and the cells on screen, down to minWidth. The first measurement may narrow a coded width. Later measurements only widen, so scrolling does not keep changing the width. A saved width (lockWidth) and a width dragged in this session stay as they are. Dragging a heading's edge writes that width and gives the difference to the widthGrow columns. Dragging the only widthGrow column moves the fixed column beside it. A horizontal bar appears only when the minimums no longer fit. Header handles are the default. Columns can be dragged; movable:false opts out. The row indicator stays frozen and does not resize. preferences.ui.tables.mail and preferences.ui.tables.accounts store the column order, the same way the icon tables do. When more than one column grows, a drag saves each of those live widths and the next open uses them as the widthGrow weights, so the share comes back and the row still fills the pane. Icons saves Name and Key. Libraries saves Name and the address. Mail saves From, Subject, and the preview that way, and the address column is stored as from. A single growing column is not saved: Accounts Name and the asset id stay the slack, and the fixed column beside them is what is saved. Saved widths are whole pixels from 30 to 4000. Reset clears them. Mail also remembers the time, the flags, and the paperclip. preferences.ui.tables.<id>.selected is the row id. preferences.ui.tables.<id>.scroll is { top, left } in pixels. Mail stores preferences.ui.tables.mail.place as { mailbox, selected, top, left } and restores that row and scroll when that folder opens. Reset does not clear the selected row or the scroll. Drawing the rows puts the holder back at the top, so the saved position is written again until the user moves the list. Up and Down move one row. Page Up and Page Down move a page of rows. Home and End move to the first and last row.",
        ],
      },
      {
        id: "icon-tables",
        title: "Icon tables",
        paragraphs: [
          "Settings → Icons, Libraries, and Assets use the same Tabulator module as Accounts (ui/src/settings/split-table.js). The column chooser opens from the row-indicator header, the same button Accounts uses. That header is 40 px wide and the button fills the cell. Every data column is resizable, and the table sets movableColumns so those columns can be dragged. The indicator stays frozen. Column order is preferences.ui.tables.<id>.order. Reset clears hidden columns, widths, and that order. It does not clear the selected row or the scroll. preferences.ui.tables.<id>.selected is the row id, and .scroll is { top, left } in pixels. Name and Key, and a library's Name and address, stretch, and a drag saves how they share the spare width. The asset id stretches and that width is not saved; dragging it saves the column beside it. On, Default, the icon, Icon Format, Group, Style, Kind, and Size remember their width. The table ids are icons, libraries, and assets. The splits are iconPane, libraryPane, and assetPane, and those ratios are restored when the app opens. Tables zoom scales those lists, and the Accounts list, through setScale. Details zoom is CSS zoom on the Accounts, Libraries, and Assets forms. The Icons editor uses Icon Wizard zoom, so Details does not scale that pane. Mail list uses the Mail list zoom through setScale, and its pane is not CSS-zoomed. A library change of URL or enabled reloads the window after prefs.ui.set. A Font Awesome style change does not, because the kit URL stays the same. The shipped hosts are kit.fontawesome.com, code.iconify.design, fonts.googleapis.com, and cdn.jsdelivr.net. A host outside that list is refused. Iconify's script may call api.iconify.design, api.simplesvg.com, and api.unisvg.com. iconify-icon and data-icon stay out of the sanitizer until icon mapping lands. A stored group and style ride on the library entry. The core leaves them unread. The window icon set is generated from src-tauri/icons/source.png, the same file as ui/public/mark.png. Help loads that file. The Mail button snippet is still pav-system:mark. The painter adds a srcset of mark-64.png, mark-128.png, and mark-256.png, with sizes 3rem, so a tab does not scale the 1024 image. Those three files are Lanczos copies with a slightly heavier rim. The splash uses the 256 px window icon. The Wizard preview column is .pav-icon-preview-col, content-box, width calc(128px + 1.3rem), with a right border that meets the toolbar rules and runs the height of the pane. The extra width is the same as the 0.65rem padding above and below, on each side, so the 128 px icon stays 128 px. .pav-icon-square is 128 px in the current theme (--pav-surface-canvas and --pav-text). .pav-icon-theme-label is Theme, Dark, and Light, each with a top and bottom border that meets the left edge. Two .pav-icon-chip elements are also 128 px, .is-dark and .is-light. Those grounds use --pav-log-print and --pav-log-print-paper. They are not buttons. The four bars are .button-bar.pav-module-toolbar. The first is .pav-icon-crumb (section, name, key, format). Close is on that bar only when the Wizard is a popup, and Close, Check, and X share the Help jump's width. The second is presets. Default stays pinned outside the scroller, and the scrollbar is hidden. Accept is button.pav-dialog-ok painted from ui.check, and Cancel is button.pav-dialog-cancel painted from ui.cancel. Each has a left border and no right border. Close keeps a right border. The third is labeled Icon Format. The fourth is the tools for that format. .pav-icon-label is Icon Format, Family, Style, and Color: font-weight 700 and color --pav-accent. .pav-icon-menu is width max-content with a right border, and .pav-icon-toolsbar > .pav-icon-toolset is min-width max-content so Family and Style do not shrink. Icon results zoom sets --pav-icon-hit-size on .pav-icon-grid from a 28 px base. preferences.ui.iconResultZoom sits beside zoom, 100–300% by 5%, default 100%, and Shell does not multiply it. Ctrl+wheel over .pav-icon-grid changes that key. .pav-icon-hit-name is the label under each hit, with an 8 px gap. The Family menu is faFamilyList(): every Pro+ family, including Graphite, when a Font Awesome object is present, and Classic, Sharp, Duotone, Sharp Duotone, and Brands when it is not. Placement still uses faCatalog(), which keeps the prefixes FontAwesome.library.definitions has filled, and that catalog is cached for the session. faIconPlace() draws a hit in the selected family and style when the kit has it there, otherwise in another loaded family and style that can draw the name. Choosing that hit selects the pair. faKitNames() adds kit names the metadata page did not return. A name in no loaded prefix is left out. When the kit definitions are empty, every catalog hit is shown. Flags is .pav-icon-flag-menu, with a checkbox in .pav-icon-flag-check. Size, rotate, and flip are one check each. Styling, width, and animation toggle. Styling is fa-swap-opacity, fa-border, and fa-inverse. The menu uses resize: both, and preferences.ui.iconFlagBox stores width and height in px, width 160–360 and height 120–880. The trigger reads Flags (N) when N is greater than zero, and .pav-icon-flag-clear, inside .pav-icon-flags and to the left of that group's right border, clears every flag. A missing box keeps the CSS size. Opening a Font Awesome icon reads its name, family, style, color, and flags, so a flag click keeps that icon. library.flags is the class list, and both fa-2x and fa-rotate-90 stay on the icon. The wizard search is a .pav-module-search after .pav-module-spacer, the same 16rem field as #icons-search. Its placeholder is 🔍 Search Font Awesome. #icons-search placeholder is 🔍 Search icons. The help icon is a .help-jump with data-help-section fontawesome, painted from nav.help. The Icons column is Icon Format, from iconFormat() in ui/src/common/icons/snippet.js. pav-system: is PNG, so Mail opens on that tab. Search results are .pav-icon-hit buttons in .pav-icon-grid. Each hit has a 10 px margin and 20 px of padding, and the grid gap is 0. That grid fills the pane. Font Awesome refreshes once when a probe for facebook does not return that name with family brands. Material refreshes once when its catalog is empty. A failed refresh is not tried again. A query matches any name, label, or keyword that contains the text. The hits are painted with the fourth toolbar's family, style, color, and flags. A row whose family is brands is drawn with fa-brands and does not take the picked family. Wizard toolbars cancel Icon Wizard zoom and take Button bars zoom, so the search field matches Search icons. The help icon is nav.help. The wizard search is flex 0 0 16rem. Flags adds Styling: fa-swap-opacity, fa-border, and fa-inverse. A size class sets font-size on the glyph. The slot sets font-size and leaves the glyph at height 1em and width auto, centered with translate so rotate and spin still use transform. fa-2x does not change the bar. Family, Style, Library, and Material style are button.pav-icon-menu beside a hidden select. The list is .pav-icon-pop, colored with --pav-surface-canvas and --pav-text. A check is the pressed row. The background is only :hover and :focus-visible.",
        ],
      },
      {
        id: "sync",
        title: "Mail sync",
        paragraphs: [
          "An enabled JMAP account starts a mail sync when the core is ready. sync.now asks again for one account, or for every enabled account when the call leaves the account id out. The states are idle, syncing, offline, locked, error:auth, error:server, and disabled. sync.state carries the account id, the state, and a short detail.",
          "A 401 leaves the account at error:auth until the password changes. A locked keyring leaves it at locked until sync.now or the next poll. This build stores the Session, the remote mail accounts, identities, and mailboxes. mailbox.list returns the tree: one account root, then each mailbox. Its unread and total are counted from local rows. total skips a message marked deleted. unread also requires not seen and not draft. The account root sums those. A parent folder does not add its children. The stored mailbox totals stay the server numbers, and the sync pie still uses those. identity.list returns the synced identities. A mailbox change emits mail.mailboxes_changed. A mailbox created on the server is stored on the next poll, which is 2 minutes. The Mail nav reads mailbox.list and patches the open tree from that event. sync.state updates the account root.",
          "tests/support/record.sh stores a scrubbed Session, Mailbox/get, and Identity/get from the Stalwart container. The fixture serves one of those files when the method and the arguments match. Every other call still uses the scripted mailboxes.",
          "A Session account id is the object key, and mail is under accountCapabilities. The suite parses the recorded Session and requires account b to keep the mail capability. The fixture Session uses that same shape, so mailbox sync runs against it.",
          "Identity/get is sent with the submission capability. Stalwart rejects the method without it. A mail account that does not advertise submission syncs mailboxes only.",
          "Mailbox sync asks Mailbox/query for a page of ids, then Mailbox/get for those ids, and repeats until the server's total is stored. The page size is the Session's maxObjectsInGet, and at most 500. A stored tree that already has that many mailboxes is checked against the server total, and a larger total walks the list again.",
          "Header sync then asks Email/query for each mailbox, newest first, Inbox and Sent before the others. The first page is that same cap, and the rest of the mailbox follows in pages of that size. Email/get asks for header fields only, in the same request. Each page that stores messages emits mail.messages_changed. The activity total starts as the sum of the folder counts. The first page of a folder replaces that folder's count with the query total. The mail nav draws that progress as a pie on the account row. The Update button draws the same percent behind its icon and puts it in the tooltip. A second click cancels the running sync. Quitting wakes the transfer so an in-flight request is dropped instead of waiting out the poll.",
          "message.list takes {view:{kind:\"mailbox\", id}} where id is the mailbox tree node id, {view:{kind:\"unified\", name}} with name inbox, unread, flagged, drafts, or sent, or {view:{kind:\"threaded\", id}}, which returns the same flat rows as mailbox until threads land. A search view is under Local search. It returns accountId, total, and messages of id, from, to, subject, preview, receivedAt, seen, flagged, answered, forwarded, hasAttachment, and accountId. from and to are the first address name, or its email, or an empty string. receivedAt is epoch milliseconds. The result accountId is the profile account for a mailbox or a flat thread, and null for a unified view. Each row still carries its own accountId. Newest receivedAt first, then email.rowid descending. The cap is preferences.cache.listRows. A further row that would push the reply past 2 MB is left out, and total stays the match count. A row whose keywords contain $deleted is omitted, and total counts the rows that remain. The keyword stays stored. The mail list loads the mailbox reply and loads it again on mail.messages_changed for that account. It does not ask for another page while scrolling. The table uses the virtual renderer, so those loaded rows are not one DOM node per message. j and k move the selection one row. Page Up, Page Down, Home, and End move it by a page or to the ends. Enter does not open a message. Clicking a row selects it. debug.seed_messages takes {mailboxId, count}, inserts that many local rows clamped to the list cap, and is absent from a build with NDEBUG. It does not use the network.",
          "message.get takes {id, allowRemote}. id is msg:<ra_id>:<emailId>. It returns the stored headers, plain text, and attachment rows (part id, name, mime, size, cid, disposition). Attachment bytes stay on the server. Text and HTML are kept up to 2 MB. The sanitizer is lexbor v3.0.0 plus the CSS filter. Settings → Mail stores preferences.mail.cssExtra, extra property names the filter unions with the built-in list. message.get still returns htmlSanitized null and hasRemote false. The reading pane, which calls the sanitizer, is not in this build. allowRemote is accepted and does not load remote images. A fetched body is cached; none, failed, evicted, and a missing file fetch again. After a sync reaches idle, fetch_bodies none, recent (received in the last 30 days), or all prefetches at most two bodies, as activity body.fetch, and yields to sync and to message.get.",
          "Once that first walk has stored an Email state, a later sync asks Email/changes and Thread/changes instead of walking every mailbox. The first of those syncs asks Thread/get for the threads already stored, saves that state, and leaves the messages in place. A later sync uses Thread/changes. Created and updated messages fetch the same headers. A destroyed message drops its row, its full-text row, its body files, and a blob file that no other message still uses. That full-text row is contentless, so the delete repeats the indexed subject, addresses, and body. A body that does not match is rolled back and tried again with an empty body, which is how a header-only row was stored. The retry is a debug log line. A flag, move, or delete made in another client shows up on the next poll, which is still 2 minutes. Folder totals come from Mailbox/get. The email sync does not add or subtract them. A changes pass reports activity detail Changes with no percent, so the account pie stays on Syncing. If the server cannot calculate changes, that type is dropped and synced again, and the log records a warning.",
          "Unity's sync waits allow 30 seconds. A page cap of 2 on the Windows fixture needs more round trips than 8 seconds, and the account is still syncing when that shorter wait ends.",
        ],
      },
      {
        id: "search",
        title: "Local search",
        paragraphs: [
          "The search field is not in the window yet. That screen is Q-2. Local search is already a command. search.run takes a query, server false (server true is refused until Q-2), an optional limit from 1 to 1000, and an optional scope of account ids or one mailbox id. It returns accountId, total, and messages in the same shape as message.list. A row whose keywords contain $deleted is omitted, and total counts the rows that remain. accountId is null when the search is not pinned to one account. Rank is bm25, then newest received_at, then row id. message.list accepts view.kind search with the same query and the same row shape, and its accountId stays null.",
          "The syntax is from:, to:, cc:, subject:, has:attachment, is:unread, is:read, is:flagged, is:answered, before:YYYY-MM-DD, after:YYYY-MM-DD, in: a mailbox name or role, account: a number or a name, a quoted phrase, bare words, -word, and OR. An unknown key: is ordinary text. Dates are local days in the profile timezone, America/Vancouver unless the profile says otherwise. before: is strictly before that local midnight. after: is that midnight or later.",
          "Header text is indexed when a message is stored. Body text is indexed when a body is fetched: the plain part when it has visible characters, otherwise the text of the HTML, capped at 256 KB. Script and style are not indexed. index.rebuild rebuilds that index as an activity. The Settings button that starts it is not in this build.",
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
          "preferences.ui is opaque to the core. The page stores zoom, nav width, splits, fonts, icons, formats, the activity view, the rail order, the shortcuts, and the open tabs there. The core does not invent missing zoom keys and does not read the tab list or the format strings. The page clamps the keys it knows and keeps the rest. log_level lives on the index because logging starts before a profile is usable. themes_folder is an optional index key. Absent, theme files sit beside Pavillion.json.",
          "The open tabs are preferences.ui.session.tabs. preferences.ui.session.restore defaults to on. preferences.ui.session.shutdown is clean after a quit and open while the app is running. Ctrl+C requests that same quit. The window stays up until that clean value is stored. A forced kill leaves it open. preferences.ui.session.maxTabs defaults to 50. Zoom, the nav width, and the theme stay in their own keys.",
          "Ctrl+C is caught in the UI process. JavaScriptCore's scavenger thread does not keep the blocked mask, so the shell installs a handler that only counts the signal and then runs the same quit as the window close. WebKitGTK 2.52 aborts its web process during EGL teardown after that save. The shell sets WEBKIT_DISABLE_COMPOSITING_MODE when it is unset, which turns accelerated compositing off. A value already in the environment is left alone. Native stderr from WebKitGTK's child processes — including JSC's \"received NeedDebuggerBreak trap\" message on inspector detachment — is captured by a pipe installed before Tauri starts, and forwarded to log.db instead of the terminal. When the core thread has joined but the WebKit web process is still tearing down, that process can raise NeedDebuggerBreak (a DebugBreak trap surfaced after shutdown) while flushing its EGL/display state; the trace log shows \"core stopped\" followed by the trap, and the app still exits with the core's exit code. Fix: at RunEvent::ExitRequested the shell calls close_devtools() and sleeps 500ms so the inspector transport drains before EGL flushes, and no longer force-kills the web process (terminate_web_process was removed because killing the web process mid-transport was what raised the trap).",
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
        id: "filters",
        title: "Row filters",
        paragraphs: [
          "Every list in Pavillion narrows the same way: a filter hides rows, it does not delete them. The selected row stays selected when it survives, and clearing the filter brings every row back. A filter never changes a row id or any backend data.",
          "Tabulator tables take a case-insensitive substring search in the .pav-module-search field on the module toolbar, applied with table.setFilter(fn) so empty groups disappear. The keyboard navigator and the module footer (Refresh, First, Previous, Next, Last) walk only the shown rows. Log view filters (severity, subsystem, text) persist in preferences.ui.log; tree filter flags persist in preferences.ui.trees.<treeId>; a module search stays for the tab.",
          "The tree nav uses preset buttons above it (Star for favorites, Heart for the second set, Ghost to hide infrequent folders) that toggle view-state flags and re-render. Those are in docs/E-TREE.md. A mail search field and saved searches land with Q-2; see docs/Q-SEARCH.md. Full rules live in docs/UI-FILTER.md.",
        ],
      },
      {
        id: "help-theme",
        title: "Help colors",
        paragraphs: [
          "Help index is the zoom key helpIndex. Help content is helpContent. Filters is the zoom key filters. It scales the Help filter strip, and later strips use the same key. All three live on preferences.ui.zoom with the other pane scales. Shell multiplies them. A missing key is filled at 100% the next time the page saves. A saved helpFilter is copied onto filters once, then dropped. The Help search bar and the filter chevron stay on the Button bars key. Each filter chip keeps a 1em square in the line so the glyph does not cover the next chip.",
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
          "Opening a profile migrates the database before the activity list is loaded. A fresh file is user_version 3. Schema v1 keeps activity, account_state, remote_account, identity, mailbox, sync_state, and kv. Schema v2 adds email, threads, body_cache, attachments, a contentless email_fts index, the outbox, and local drafts. Schema v3 adds list_id and auth_summary on email. An activity table already in the file is kept. A database newer than this binary does not open. Message bodies and attachments are files under the profile blobs directory, mode 0600. Cache limits default to 2048 MB of bodies and 4096 MB of blobs. Settings → Limits labels them Database and Document and writes the same keys. Message list is preferences.cache.listRows, default 10000, from 1 to 10000. message.list uses that cap. It is not a disk budget. Refresh there calls cache.usage for the bytes on disk, volumeFreeBytes, and, per account, the quick server counts for mail, contacts, calendars, and files. The page puts on-disk size and volumeFreeBytes on one line beside each cap, builds two account columns, and a Total row that sums each column on its own. Database entries are the mail, contact, and calendar counts. Database size adds mail.bodiesBytes and reported contact and calendar sizes. Document entries are mail.attachmentsCount plus the file count. Document size adds mail.attachmentsBytes and reported file sizes. A pool size stays blank until that mail split is known, and so does the Document entry count. Check sizes passes measure and walks Email and FileNode sizes the quota does not split, several windows per request and at most 4 for mail. The mail get also sums decoded text and HTML, and decoded attachments, and counts attachment parts, stored as mailBodiesBytes, mailAttachmentsBytes, and mailAttachmentsCount. cache.change reports progress. Cancel is cache.usage with cancel true and stops the walk. The last finished walk stays in kv limits.server. Refresh during a check does not drop the result. A blank cell was not reported. The page does not show serverBytes. IMAP, CalDAV, and CardDAV use the same fields later and stay blank in this phase. An over-limit pass after sync is the activity Evict cache.",
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
