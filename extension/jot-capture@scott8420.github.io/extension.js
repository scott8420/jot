// Jot Capture -- a tile in GNOME's quick settings panel that files a thought
// into jot's Inbox without opening jot.
//
// It does no filing of its own. Every capture is `jot --capture "text"`:
//   - jot running  -> the running jot files it and prints "Filed to the Inbox."
//   - jot closed   -> jot spools it and prints "Filed. jot will pick it up ..."
// so the tile and the command line can never disagree about what a capture is.
// The tile shows whatever jot printed.
//
// Which jot: the one the desktop entry launches (install-desktop.sh bakes the
// build tree's binary into it), else `jot` on PATH.

import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import * as QuickSettings from 'resource:///org/gnome/shell/ui/quickSettings.js';

const APP_ID = 'io.github.scott8420.Jot';

function jotAppInfo() {
    return Gio.DesktopAppInfo.new(`${APP_ID}.desktop`);
}

// The binary to run. The desktop entry's Exec, when it is installed; `jot`
// on PATH otherwise.
function jotExecutable() {
    const info = jotAppInfo();
    if (info) {
        const exe = info.get_executable();
        if (exe && GLib.file_test(exe, GLib.FileTest.IS_EXECUTABLE))
            return exe;
    }
    return GLib.find_program_in_path('jot');
}

// A thought that starts with '-' would be read by jot as an option. Leading
// dashes and bullets are list habit, not content, so they go.
function cleaned(text) {
    return text.replace(/\s+/g, ' ').replace(/^[\s\-\u2013\u2014\u2022*]+/, '').trim();
}

// Words, with "quoted phrases" (straight or curly double quotes) kept as one.
// Not single quotes: "don't" is a word, not the start of a phrase.
export function tokens(text) {
    const out = [];
    const re = /["\u201c]([^"\u201d]*)["\u201d]?|(\S+)/g;
    let m;
    while ((m = re.exec(text)) !== null) {
        // A quoted "" stays, as '' -- after -al it means "no line" (s061f).
        if (m[1] !== undefined)
            out.push(m[1].trim());
        else if (m[2] !== undefined && m[2].trim() !== '')
            out.push(m[2].trim());
    }
    return out;
}

const LIST = ['-l', '--list'];
const APPEND = ['-a', '--append'];
const BOTH = ['-al', '-la'];
const FLAGS = [...LIST, ...APPEND, ...BOTH];

// What the line says -> jot's argv, the command line's forms:
//   ring the vet                       jot --capture "ring the vet"
//   Groceries -l milk eggs "rye bread"  jot Groceries --list milk eggs "rye bread"
//   Groceries -a check the pantry      jot Groceries --append "check the pantry"
//   Groceries -al "for Sat" milk eggs  jot Groceries -al "for Sat" milk eggs
//   Groceries -a for Sat -l milk eggs  (the same -- each flag owns the words after it)
// Returns {argv} or {error}. Pure, so it is tested outside the shell.
export function argvFor(text) {
    const t = tokens(text);
    if (t.length === 0)
        return {argv: null};
    if (FLAGS.includes(t[0]))
        return {error: 'Name the note first: Groceries -l milk eggs'};
    // A command only when the SECOND word is a flag: "what is -l about" is a
    // thought, as it always was.
    const flags = t.length > 1 && FLAGS.includes(t[1]) ? t.slice(1).filter(w => FLAGS.includes(w)) : [];
    const hasA = flags.some(f => APPEND.includes(f));
    const hasL = flags.some(f => LIST.includes(f));
    const hasBoth = flags.some(f => BOTH.includes(f)) || (hasA && hasL);

    if (hasBoth) {
        let line = '';
        const items = [];
        if (BOTH.includes(t[1]) && !hasA && !hasL) {
            line = t.length > 2 ? cleaned(t[2]) : '';
            items.push(...t.slice(3).map(cleaned).filter(w => w !== ''));
        } else {
            let to = 'name';
            const words = [];
            for (const w of t.slice(1)) {
                if (APPEND.includes(w)) { to = 'line'; continue; }
                if (LIST.includes(w) || BOTH.includes(w)) { to = 'items'; continue; }
                if (to === 'items') items.push(cleaned(w));
                else words.push(w);
            }
            line = cleaned(words.join(' '));
        }
        const live = items.filter(w => w !== '');
        const name = cleaned(t[0]) || t[0];
        // s061f: -al "" milk eggs -- no line, just the todos. A line and no
        // items is just the line.
        if (!line && live.length > 0)
            return {argv: [name, '--list', ...live]};
        if (line && live.length === 0)
            return {argv: [name, '--append', line]};
        if (!line)
            return {error: `${t[0]} -al needs an item or more`};
        return {argv: [name, '-al', line, ...live]};
    }

    const flag = t.length > 1 ? t[1] : '';
    const isList = LIST.includes(flag);
    if (isList || APPEND.includes(flag)) {
        // A list's items are its words (quotes join them); an append is the
        // rest of the line as typed.
        const rest = isList
            ? t.slice(2).map(cleaned).filter(w => w !== '')
            : [cleaned(text.replace(/^.*?\s(?:-a|--append)(?:\s|$)/, ''))].filter(w => w !== '');
        if (rest.length === 0)
            return {error: isList ? `${t[0]} -l needs an item or more`
                : `${t[0]} -a needs some words`};
        return {argv: [cleaned(t[0]) || t[0], isList ? '--list' : '--append', ...rest]};
    }
    const thought = cleaned(text);
    return thought ? {argv: ['--capture', thought]} : {argv: null};
}

// The fold-out under the line: what each form does.
const HELP = [
    ['ring the vet', 'a note in the Inbox'],
    ['Groceries -l milk eggs "rye bread"', 'todos in the note Groceries (made if new)'],
    ['Groceries -a check the pantry', 'a line of text in Groceries'],
    ['Groceries -al "for Saturday" milk eggs', 'a line, then todos under it, at once ("" for no line)'],
    ['ring the vet #pets', 'tagged #pets -- in a list, a lone #errands tags the note'],
];

const JotCaptureToggle = GObject.registerClass(
class JotCaptureToggle extends QuickSettings.QuickMenuToggle {
    constructor(extension) {
        const gicon = Gio.icon_new_for_string(
            `${extension.path}/icons/jot-logo-symbolic.svg`);
        super({
            title: 'Jot',
            subtitle: 'Capture',
            gicon,
            toggleMode: false,
        });

        this._busy = false;
        this._clearId = 0;

        this.menu.setHeader(gicon, 'Jot', 'Capture to the Inbox');

        // ── the line ───────────────────────────────────────────────────────
        const lineItem = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            can_focus: false,
            style_class: 'jot-capture-item',
        });
        this._entry = new St.Entry({
            hint_text: 'Capture a thought\u2026  (or Groceries -l milk)',
            can_focus: true,
            x_expand: true,
            style_class: 'jot-capture-entry',
        });
        this._entry.clutter_text.connect('activate', () => this._capture());
        lineItem.add_child(this._entry);
        this.menu.addMenuItem(lineItem);

        // ── what jot said ──────────────────────────────────────────────────
        this._status = new St.Label({
            text: 'Enter files it. Esc closes.',
            style_class: 'jot-capture-status',
            x_expand: true,
        });
        const statusItem = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            can_focus: false,
        });
        statusItem.add_child(this._status);
        this.menu.addMenuItem(statusItem);

        // ── what can I type? ───────────────────────────────────────────────
        const help = new PopupMenu.PopupSubMenuMenuItem('What can I type?');
        for (const [example, meaning] of HELP) {
            const row = new PopupMenu.PopupBaseMenuItem({
                reactive: false,
                can_focus: false,
                style_class: 'jot-capture-help-row',
            });
            const box = new St.BoxLayout({vertical: true, x_expand: true});
            box.add_child(new St.Label({text: example, style_class: 'jot-capture-help-example'}));
            box.add_child(new St.Label({text: meaning, style_class: 'jot-capture-help-meaning'}));
            row.add_child(box);
            help.menu.addMenuItem(row);
        }
        const tip = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false,
            style_class: 'jot-capture-help-row'});
        tip.add_child(new St.Label({
            text: 'Quote a name or an item of several words.\nSame as jot on the command line.',
            style_class: 'jot-capture-help-meaning',
        }));
        help.menu.addMenuItem(tip);
        this.menu.addMenuItem(help);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        // ── open jot ───────────────────────────────────────────────────────
        this.menu.addAction('Open jot', () => this._openJot());

        // The tile's body opens the line too: there is nothing to toggle.
        this.connect('clicked', () => this.menu.open());

        // The cursor is in the line whenever the menu opens.
        this.menu.connect('open-state-changed', (_menu, open) => {
            if (!open)
                return;
            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                this._entry.grab_key_focus();
                return GLib.SOURCE_REMOVE;
            });
        });
    }

    _say(text, error = false) {
        this._status.text = text;
        if (error)
            this._status.add_style_class_name('jot-capture-error');
        else
            this._status.remove_style_class_name('jot-capture-error');
    }

    _capture() {
        if (this._busy)
            return;
        const said = argvFor(this._entry.get_text());
        if (said.error) {
            this._say(said.error, true);
            return;
        }
        if (!said.argv)
            return;

        const exe = jotExecutable();
        if (!exe) {
            this._say('jot not found -- run ./install.sh', true);
            return;
        }

        this._busy = true;
        this._entry.reactive = false;
        let proc;
        try {
            proc = Gio.Subprocess.new([exe, ...said.argv],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE);
        } catch (e) {
            this._done(false, `Could not run jot: ${e.message}`);
            return;
        }
        proc.communicate_utf8_async(null, null, (p, res) => {
            try {
                const [, out, err] = p.communicate_utf8_finish(res);
                if (p.get_successful())
                    this._done(true, (out || '').trim() || 'Filed.');
                else
                    this._done(false, (err || '').trim() || 'jot could not file it.');
            } catch (e) {
                this._done(false, e.message);
            }
        });
    }

    // The line clears only when jot took it: a failed capture keeps its words.
    _done(ok, said) {
        this._busy = false;
        this._entry.reactive = true;
        if (ok)
            this._entry.set_text('');
        this._say(said, !ok);
        this._entry.grab_key_focus();
        console.log(`jot-capture: ${ok ? 'ok' : 'failed'} -- ${said}`);

        if (this._clearId)
            GLib.source_remove(this._clearId);
        this._clearId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 6, () => {
            this._clearId = 0;
            this._say('Enter files it. Esc closes.');
            return GLib.SOURCE_REMOVE;
        });
    }

    _openJot() {
        const info = jotAppInfo();
        try {
            if (info) {
                info.launch([], global.create_app_launch_context(0, -1));
            } else {
                const exe = jotExecutable();
                if (!exe)
                    throw new Error('jot not found');
                Gio.Subprocess.new([exe], Gio.SubprocessFlags.NONE);
            }
            Main.panel.closeQuickSettings?.();
        } catch (e) {
            this._say(`Could not open jot: ${e.message}`, true);
        }
    }

    destroy() {
        if (this._clearId) {
            GLib.source_remove(this._clearId);
            this._clearId = 0;
        }
        super.destroy();
    }
});

const JotCaptureIndicator = GObject.registerClass(
class JotCaptureIndicator extends QuickSettings.SystemIndicator {
    constructor(extension) {
        super();
        this.quickSettingsItems.push(new JotCaptureToggle(extension));
    }

    destroy() {
        this.quickSettingsItems.forEach(item => item.destroy());
        super.destroy();
    }
});

export default class JotCaptureExtension extends Extension {
    enable() {
        this._indicator = new JotCaptureIndicator(this);
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
    }

    disable() {
        this._indicator?.destroy();
        this._indicator = null;
    }
}
