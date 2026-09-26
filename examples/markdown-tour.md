# The jot markdown tour

(Got here by **Import Markdown…** — Ctrl+Shift+M, or drag a .md file from Files onto the tree. Dropped on a note, it lands under that note.)

Read this note twice: once as it is (**Source**, every code on screen), then press **Ctrl+E** for **Reading** (the codes gone). Press Ctrl+E again to come back. Double-click any word in Reading to jump here to that spot.

---

## Headings

A line that starts with hashes and a space is a heading. More hashes, smaller heading.

# Heading 1
## Heading 2
### Heading 3
#### Heading 4

## Emphasis

Wrap words to style them: **bold with two stars**, __or two underscores__, *italic with one star*, _or one underscore_, and ~~struck through with two tildes~~.

A backslash shows a code as itself: \*not italic\*.

## Lists

A dash and a space makes a bullet. Two spaces in front indents it under the one above.

- groceries
  - milk
  - eggs
    - a dozen, brown
- hardware store

A star or a plus works the same:

* star bullet
+ plus bullet

Numbers and a period (or a parenthesis):

1. first
2. second
3) third

## Todos

A bullet with a box. In Reading, click the box to tick it.

- [ ] call the vet #errands
- [ ] renew the registration
- [x] mow the lawn
  - [ ] an indented todo, under the one above

A tag is a hash with NO space, straight into a word: #inbox #someday. (A hash WITH a space is a heading.)

## Quotes

A greater-than sign and a space:

> Plans are nothing; planning is everything.

## Code

Inline: put `backticks` around a word or `a short command`.

A block: three backticks on a line of their own, the code, then three more. A word after the first three names the language.

```cpp
int main() {
    return 0;
}
```

In Reading, a block sits in a bubble with a **Copy** button in its corner: it copies just the code, no backticks.

## Links

A label in square brackets, the address in parentheses: [GNOME](https://www.gnome.org).

To link to another note: select it in the tree, **Ctrl+Shift+C** (Copy link), and paste it here. It looks like `[Title](jot:...)`, and in Reading a click opens that note.

## Pictures

An exclamation mark in front of a link makes it a picture: `![label](path)`.

Easier: drag an image from Files onto this note. jot copies it in and writes the line for you. Hold **Shift** while dropping to link to the file where it lives instead of copying it.

## Horizontal rule

Three dashes on a line by themselves:

---

That's the whole set. Everything above is plain text in the file; jot only changes how it looks.
