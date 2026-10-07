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
    return text.replace(/\s+/g, ' ').replace(/^[\s\-–—•*]+/, '').trim();
}

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
            hint_text: 'Capture a thought…',
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
        const text = cleaned(this._entry.get_text());
        if (!text)
            return;

        const exe = jotExecutable();
        if (!exe) {
            this._say('jot not found -- run install-desktop.sh', true);
            return;
        }

        this._busy = true;
        this._entry.reactive = false;
        let proc;
        try {
            proc = Gio.Subprocess.new([exe, '--capture', text],
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
