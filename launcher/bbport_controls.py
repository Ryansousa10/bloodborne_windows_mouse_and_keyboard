# SPDX-License-Identifier: GPL-2.0-or-later
"""Controls page of the Windows launcher: the keyboard and mouse bindings in keybinds.ini.

src/runtime_pad.c reads the file at start and writes its defaults when it is missing; ACTIONS
and OPTIONS below follow its actions[] and kbm defaults (Dark Souls III layout)."""

from pathlib import Path

VERSION = 3
SLOTS = 3  # inputs shown per action (the game accepts up to 6)

# (ini name, default inputs, (English, Russian) label), grouped as on the page.
GROUPS = [
    (('Movement and camera', 'Движение и камера'), [
        ('move_forward', 'w', ('Move forward', 'Вперёд')),
        ('move_back', 's', ('Move back', 'Назад')),
        ('move_left', 'a', ('Move left', 'Влево')),
        ('move_right', 'd', ('Move right', 'Вправо')),
        ('walk', 'lalt', ('Walk (hold)', 'Шаг (удерживать)')),
        ('circle', 'space escape', ('Dodge / dash (hold) / jump while dashing; back in menus',
                                    'Уклонение / бег (удерживать) / прыжок на бегу; назад в меню')),
        ('l3', 'c', ('Jump while dashing (L3)', 'Прыжок на бегу (L3)')),
        ('r3', 'q mouse_middle', ('Lock on / reset camera (R3)', 'Захват цели / сброс камеры (R3)')),
        ('camera_up', 'i', ('Camera up', 'Камера вверх')),
        ('camera_down', 'k', ('Camera down', 'Камера вниз')),
        ('camera_left', 'j', ('Camera left', 'Камера влево')),
        ('camera_right', 'l', ('Camera right', 'Камера вправо')),
    ]),
    (('Combat', 'Бой'), [
        ('r1', 'mouse_left', ('Attack (R1)', 'Атака (R1)')),
        ('r2', 'shift+mouse_left', ('Strong attack (R2)', 'Сильная атака (R2)')),
        ('l1', 'mouse_right', ('Transform weapon (L1)', 'Трансформация оружия (L1)')),
        ('l2', 'shift+mouse_right lctrl', ('Firearm (L2)', 'Огнестрельное оружие (L2)')),
    ]),
    (('Items and menus', 'Предметы и меню'), [
        ('cross', 'e enter', ('Interact; confirm in menus (Cross)', 'Действие; подтвердить в меню (Крест)')),
        ('square', 'r', ('Use quick item (Square)', 'Быстрый предмет (Квадрат)')),
        ('triangle', 'f', ('Blood vial (Triangle)', 'Флакон крови (Треугольник)')),
        ('up', 'up wheel_up', ('D-pad up', 'Крестовина вверх')),
        ('down', 'down wheel_down', ('Switch quick item (d-pad down)', 'Сменить быстрый предмет (вниз)')),
        ('left', 'left shift+wheel_down', ('Switch left hand weapon (d-pad left)', 'Сменить оружие в левой руке (влево)')),
        ('right', 'right shift+wheel_up', ('Switch right hand weapon (d-pad right)', 'Сменить оружие в правой руке (вправо)')),
        ('options', 'tab', ('Game menu (Options)', 'Меню игры (Options)')),
        ('touchpad', 'g', ('Gestures (touchpad)', 'Жесты (тачпад)')),
        ('touchpad_right', 'backspace', ('Right half of the touchpad (debug menu)', 'Правая половина тачпада (debug menu)')),
    ]),
]
ACTIONS = [action for _title, actions in GROUPS for action in actions]
DEFAULTS = {name: inputs.split() for name, inputs, _label in ACTIONS}

# (key, default, minimum, maximum or None for a switch, (English, Russian), hint)
OPTIONS = [
    ('mouse_camera', 1, 0, None, ('The mouse turns the camera', 'Мышь поворачивает камеру'), None),
    ('mouse_sensitivity', 1.0, 0.1, 5.0, ('Mouse sensitivity', 'Чувствительность мыши'),
     ('The same scale as Deadlock and other Source games: their sensitivity value turns the camera as '
      'far here.', 'Та же шкала, что в Deadlock и других играх на Source: их значение поворачивает камеру '
      'так же далеко.')),
    ('mouse_sensitivity_y', 1.0, 0.2, 3.0, ('Vertical sensitivity (× horizontal)', 'Вертикальная чувствительность (× горизонтальной)'), None),
    ('mouse_invert_x', 0, 0, None, ('Invert horizontal', 'Инвертировать по горизонтали'), None),
    ('mouse_invert_y', 0, 0, None, ('Invert vertical', 'Инвертировать по вертикали'), None),
    ('mouse_no_auto_rotation', 1, 0, None, ('No camera auto-rotation while moving (as in PC games)',
                                            'Без автоповорота камеры при движении (как в играх на ПК)'),
     ('The game turns the camera after the character as it moves; off, only the mouse or the stick '
      'turns it. Needs a restart.', 'Игра поворачивает камеру вслед за персонажем; выключено — '
      'камеру поворачивают только мышь или стик. Нужен перезапуск.')),
    ('walk_tilt', 0.4, 0.1, 1.0, ('Walking speed (stick tilt)', 'Скорость шага (наклон стика)'), None),
    ('ds3_jump', 1, 0, None, ('Jump with the dodge key while dashing, as in Dark Souls III',
                              'Прыжок клавишей уклонения на бегу, как в Dark Souls III'),
     ('Off: pressing it again while dashing does the game\'s sprint roll.',
      'Выкл.: повторное нажатие на бегу делает перекат.')),
]
# Keys of earlier files read as today's; they win over the current key wherever they stand (version 2
# kept the mouse camera's sensitivity apart from a stick one).
LEGACY = {'mouse_direct_sensitivity': 'mouse_sensitivity'}

MODIFIERS = {'lshift': 'shift', 'rshift': 'shift', 'lctrl': 'ctrl', 'rctrl': 'ctrl', 'lalt': 'alt', 'ralt': 'alt'}
# Tk keysym -> runtime_pad.c input name (letters and digits come from the key code).
KEYSYMS = {
    'space': 'space', 'Return': 'enter', 'KP_Enter': 'keypad_enter', 'Escape': 'escape', 'Tab': 'tab',
    'BackSpace': 'backspace', 'Delete': 'delete', 'Home': 'home', 'End': 'end', 'Prior': 'pageup',
    'Next': 'pagedown', 'Up': 'up', 'Down': 'down', 'Left': 'left', 'Right': 'right',
    'Caps_Lock': 'capslock', 'Shift_L': 'lshift', 'Shift_R': 'rshift', 'Control_L': 'lctrl',
    'Control_R': 'rctrl', 'Alt_L': 'lalt', 'Alt_R': 'ralt', 'comma': 'comma', 'period': 'period',
    'slash': 'slash', 'semicolon': 'semicolon', 'apostrophe': 'apostrophe', 'quoteright': 'apostrophe',
    'bracketleft': 'leftbracket', 'bracketright': 'rightbracket', 'minus': 'minus', 'equal': 'equals',
    'grave': 'grave', 'quoteleft': 'grave', 'backslash': 'backslash', 'KP_Add': 'keypad_+',
    'KP_Subtract': 'keypad_-', 'KP_Multiply': 'keypad_*', 'KP_Divide': 'keypad_/', 'KP_Decimal': 'keypad_.',
    **{f'F{n}': f'f{n}' for n in range(1, 13)}, **{f'KP_{n}': f'keypad_{n}' for n in range(10)},
}
MOUSE_BUTTONS = {1: 'mouse_left', 2: 'mouse_middle', 3: 'mouse_right', 4: 'mouse_x1', 5: 'mouse_x2'}


def split_mods(token):
    """('shift+mouse_left') -> (['shift'], 'mouse_left'), as runtime_pad.c parse_input."""
    mods = []
    while True:
        for mod in ('shift', 'ctrl', 'alt'):
            if token.lower().startswith(mod + '+'):
                mods.append(mod)
                token = token[len(mod) + 1:]
                break
        else:
            return mods, token


def load(path):
    """(bindings: name -> [inputs], options: key -> value) from keybinds.ini over the defaults."""
    binds = {name: list(inputs) for name, inputs in DEFAULTS.items()}
    options = {key: default for key, default, *_rest in OPTIONS}
    legacy = set()
    try:
        lines = Path(path).read_text(encoding='utf-8').splitlines()
    except OSError:
        return binds, options
    for line in lines:
        line = line.split('#', 1)[0]
        if '=' not in line:
            continue
        key, value = (part.strip() for part in line.split('=', 1))
        key = key.lower()
        if key in LEGACY:
            key = LEGACY[key]
            legacy.add(key)
        elif key in legacy:
            continue
        if key in binds:
            binds[key] = value.replace(',', ' ').split()
        elif key in options:
            try:
                options[key] = type(options[key])(float(value))
            except ValueError:
                pass
    return binds, options


def save(path, binds, options):
    out = ['# bbport keyboard and mouse (Dark Souls III layout), written by the launcher\'s',
           '# Controls page; the game reads it at start. Delete it to get the defaults back.',
           '# action = inputs separated by spaces; shift+, ctrl+ or alt+ make combinations.',
           '', f'keybinds_version = {VERSION}', '']
    for name, _inputs, label in ACTIONS:
        out.append(f'{name:<15}= {" ".join(binds.get(name, [])):<26} # {label[0]}')
    out.append('')
    for key, default, _lo, high, _title, _hint in OPTIONS:
        value = options.get(key, default)
        out.append(f'{key:<19} = {int(bool(value)) if high is None else f"{float(value):.2f}"}')
    Path(path).write_text('\n'.join(out) + '\n', encoding='utf-8')


class ControlsPage:
    """Builds the page into `frame` (a scrolled page of the launcher) and saves on collect()."""

    def __init__(self, launcher, frame, path, translate, colors):
        self.launcher, self.frame, self.path, self._ = launcher, frame, Path(path), translate
        self.tk, self.ttk = launcher.tk, launcher.ttk
        self.colors = colors
        self.binds, values = load(self.path)
        self.option_vars = {}
        for key, default, _lo, high, _title, _hint in OPTIONS:
            value = values[key]
            self.option_vars[key] = self.tk.BooleanVar(value=bool(value)) if high is None else \
                self.tk.DoubleVar(value=float(value))
        self.slot_buttons = {}
        self.build()

    # ---- display ------------------------------------------------------------------------------
    def pretty(self, token):
        _ = self._
        mods, base = split_mods(token)
        names = {
            'mouse_left': _('Left click', 'Левая кнопка'), 'mouse_right': _('Right click', 'Правая кнопка'),
            'mouse_middle': _('Wheel click', 'Нажатие колеса'), 'mouse_x1': _('Mouse 4', 'Мышь 4'),
            'mouse_x2': _('Mouse 5', 'Мышь 5'), 'wheel_up': _('Wheel up', 'Колесо вверх'),
            'wheel_down': _('Wheel down', 'Колесо вниз'), 'space': _('Space', 'Пробел'), 'escape': 'Esc',
            'enter': 'Enter', 'lshift': _('Left Shift', 'Левый Shift'), 'rshift': _('Right Shift', 'Правый Shift'),
            'lctrl': _('Left Ctrl', 'Левый Ctrl'), 'rctrl': _('Right Ctrl', 'Правый Ctrl'),
            'lalt': _('Left Alt', 'Левый Alt'), 'ralt': _('Right Alt', 'Правый Alt'),
            'up': '↑', 'down': '↓', 'left': '←', 'right': '→', 'backspace': 'Backspace', 'tab': 'Tab',
            'comma': ',', 'period': '.', 'slash': '/', 'semicolon': ';', 'apostrophe': "'",
            'leftbracket': '[', 'rightbracket': ']', 'minus': '-', 'equals': '=', 'grave': '`',
            'backslash': '\\', 'pageup': 'Page Up', 'pagedown': 'Page Down', 'capslock': 'Caps Lock',
        }
        text = names.get(base) or (base.upper() if len(base) == 1 else base.replace('_', ' ').title())
        return ' + '.join([m.capitalize() for m in mods] + [text])

    def refresh(self):
        used = {}
        for name, inputs in self.binds.items():
            for token in inputs:
                used.setdefault(token, []).append(name)
        for (name, slot), button in self.slot_buttons.items():
            inputs = self.binds.get(name, [])
            if slot < len(inputs):
                token = inputs[slot]
                button.configure(text=self.pretty(token) + ('  ⚠' if len(used[token]) > 1 else ''))
            else:
                button.configure(text='—')

    # ---- page ---------------------------------------------------------------------------------
    def build(self):
        ttk, f, _, launcher = self.ttk, self.frame, self._, self.launcher
        launcher.note(f, _('Dark Souls III layout by default; the keyboard and mouse work together with a '
                           'controller. Click a field and press a key or a mouse button (hold Shift, Ctrl or '
                           'Alt for a combination). ⚠ marks an input used by more than one action. Saved '
                           'when you press PLAY; the game reads it at start.',
                           'По умолчанию раскладка Dark Souls III; клавиатура и мышь работают вместе с '
                           'геймпадом. Нажмите на поле и затем клавишу или кнопку мыши (Shift, Ctrl или Alt — '
                           'для сочетания). ⚠ — ввод занят несколькими действиями. Сохраняется при нажатии '
                           'ИГРАТЬ; игра читает файл при запуске.'), top=4)
        for (title, actions) in GROUPS:
            launcher.section(f, _(*title))
            for name, _inputs, label in actions:
                holder = ttk.Frame(f)
                for slot in range(SLOTS):
                    button = ttk.Button(holder, width=17, command=lambda n=name, s=slot, l=label: self.capture(n, s, l))
                    button.pack(side='left', padx=(0, 6))
                    self.slot_buttons[name, slot] = button
                launcher.row(f, _(*label), holder)
        launcher.section(f, _('Mouse', 'Мышь'))
        for key, _default, low, high, title, hint in OPTIONS:
            var = self.option_vars[key]
            if high is None:
                self.switch(f, var, _(*title), _(*hint) if hint else None)
                continue
            holder = ttk.Frame(f)
            ttk.Scale(holder, from_=low, to=high, variable=var, length=300).pack(side='left')
            value = ttk.Label(holder, width=6)
            value.pack(side='left', padx=10)
            show = lambda *_a, v=var, l=value: l.configure(text=f'{v.get():.2f}')
            var.trace_add('write', show)
            show()
            launcher.row(f, _(*title), holder, _(*hint) if hint else None)
        holder = ttk.Frame(f)
        holder.grid(row=launcher.next_row(f), column=0, columnspan=2, sticky='w', pady=(18, 0))
        ttk.Button(holder, text=_('Restore defaults', 'Вернуть настройки по умолчанию'),
                   command=self.restore).pack(side='left')
        launcher.note(f, _("Alt+Tab or Insert (the port's menu) frees the mouse in the game.",
                           'Alt+Tab или Insert (меню порта) освобождают мышь в игре.'))
        self.refresh()

    def switch(self, parent, var, title, hint):
        ttk, launcher = self.ttk, self.launcher
        r = launcher.next_row(parent)
        ttk.Checkbutton(parent, text=title, variable=var).grid(row=r, column=0, columnspan=2, sticky='w', pady=(4, 0))
        if hint:
            ttk.Label(parent, text=hint, style='Muted.TLabel', wraplength=launcher.px(640), justify='left').grid(
                row=r + 1, column=0, columnspan=2, sticky='w', padx=(26, 0))

    def restore(self):
        self.binds = {name: list(inputs) for name, inputs in DEFAULTS.items()}
        for key, default, _lo, high, _title, _hint in OPTIONS:
            self.option_vars[key].set(bool(default) if high is None else float(default))
        self.refresh()

    def set_slot(self, name, slot, token):
        inputs = self.binds.setdefault(name, [])
        if token is not None and token in inputs:  # already bound to this action
            return
        if token is None:
            if slot < len(inputs):
                del inputs[slot]
        elif slot < len(inputs):
            inputs[slot] = token
        elif token not in inputs:
            inputs.append(token)
        self.refresh()

    def capture(self, name, slot, label):
        """A small modal window that takes the next key, mouse button or wheel step."""
        tk, ttk, _ = self.tk, self.ttk, self._
        root = self.launcher.root
        win = tk.Toplevel(root)
        win.title(_(*label))
        win.configure(bg=self.colors['panel'])
        win.transient(root)
        win.resizable(False, False)
        ttk.Label(win, text=_(*label), style='Section.TLabel').pack(padx=24, pady=(18, 4), anchor='w')
        area = tk.Label(win, text=_('Press a key, or click or scroll here with the mouse.',
                                    'Нажмите клавишу или щёлкните / прокрутите здесь мышью.'),
                        bg=self.colors['card'], fg=self.colors['text'], width=48, height=6,
                        font=('Segoe UI', 11), cursor='hand2', wraplength=self.launcher.px(380))
        area.pack(padx=24, pady=8)
        buttons = ttk.Frame(win)
        buttons.pack(fill='x', padx=24, pady=(4, 18))
        mods, alone = set(), [None]

        def done(token):
            win.grab_release()
            win.destroy()
            if token is not False:
                self.set_slot(name, slot, token)

        def combo(base):
            order = [m for m in ('shift', 'ctrl', 'alt') if m in mods]
            return '+'.join(order + [base])

        def key_down(event):
            if 0x41 <= event.keycode <= 0x5a or 0x30 <= event.keycode <= 0x39:
                base = chr(event.keycode).lower()  # Windows virtual key: the key, whatever Shift does
            else:
                base = KEYSYMS.get(event.keysym)
            if base in MODIFIERS:
                mods.add(MODIFIERS[base])
                alone[0] = base
                return 'break'
            alone[0] = None
            if base == 'insert' or event.keysym == 'Insert':
                area.configure(text=_('Insert opens the port\'s menu in the game; choose another key.',
                                      'Insert открывает меню порта в игре; выберите другую клавишу.'))
            elif not base:
                area.configure(text=_('This key is not supported; choose another.',
                                      'Эта клавиша не поддерживается; выберите другую.'))
            else:
                done(combo(base))
            return 'break'

        def key_up(event):
            base = KEYSYMS.get(event.keysym)
            if base in MODIFIERS:
                if alone[0] == base:
                    done(base)  # the modifier on its own
                    return 'break'
                mods.discard(MODIFIERS[base])
            return 'break'

        def click(event):
            base = MOUSE_BUTTONS.get(event.num)
            if base:
                done(combo(base))
            return 'break'

        def scroll(event):
            done(combo('wheel_up' if event.delta > 0 else 'wheel_down'))
            return 'break'

        ttk.Button(buttons, text=_('Clear', 'Очистить'), command=lambda: done(None)).pack(side='left')
        ttk.Button(buttons, text=_('Cancel', 'Отмена'), command=lambda: done(False)).pack(side='right')
        win.bind('<KeyPress>', key_down)
        win.bind('<KeyRelease>', key_up)
        area.bind('<ButtonPress>', click)
        area.bind('<MouseWheel>', scroll)
        win.protocol('WM_DELETE_WINDOW', lambda: done(False))
        win.update_idletasks()
        x = root.winfo_rootx() + (root.winfo_width() - win.winfo_width()) // 2
        y = root.winfo_rooty() + (root.winfo_height() - win.winfo_height()) // 3
        win.geometry(f'+{x}+{y}')
        win.grab_set()
        win.focus_force()

    def collect(self):
        options = {}
        for key, default, _lo, high, _title, _hint in OPTIONS:
            try:
                options[key] = self.option_vars[key].get()
            except self.tk.TclError:
                options[key] = default
        save(self.path, self.binds, options)
