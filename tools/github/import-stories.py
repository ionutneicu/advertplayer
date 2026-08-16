#!/usr/bin/env python3
"""Turn docs/project-plan/stories.md into GitHub issues.

The markdown stays the source of record. This reads it, and either prints what
it would create (the default) or creates the issues with `gh`.

Acceptance-criteria tables become task lists, so each criterion is a checkbox
that can be ticked on the issue as it is met -- which is the point of importing
them at all.

Usage:
    tools/github/import-stories.py                 # dry run, prints everything
    tools/github/import-stories.py --create        # creates the issues
    tools/github/import-stories.py --create --only S-02

Requires `gh` for --create, authenticated as a user who can write issues:
    gh auth login
"""

import argparse
import pathlib
import re
import subprocess
import sys

STORIES = pathlib.Path("docs/project-plan/stories.md")
LABELS = ["story", "poc"]
MILESTONE = "PoC"


def parse_stories(text):
    """Yield (id, title, body_markdown) for each story section."""
    # Sections look like:  #### S-02 — Raspberry Pi / DispmanX spike
    pattern = re.compile(r"^#### (S-\d+) — (.+?)$", re.M)
    matches = list(pattern.finditer(text))
    for index, match in enumerate(matches):
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        yield match.group(1), match.group(2).strip(), text[start:end].strip()


def table_rows(section, header_word):
    """Rows of the first markdown table whose header contains header_word."""
    rows = []
    in_table = False
    for line in section.split("\n"):
        stripped = line.strip()
        if not stripped.startswith("|"):
            if in_table:
                break
            continue
        cells = [c.strip() for c in stripped.strip("|").split("|")]
        if not in_table:
            if header_word.lower() in " ".join(cells).lower():
                in_table = True
            continue
        if set("".join(cells)) <= set("-: "):  # separator row
            continue
        rows.append(cells)
    return rows


def build_body(story_id, section):
    """Issue body: the prose, then criteria as a checkable task list."""
    criteria = table_rows(section, "criterion")

    # Everything before the acceptance-criteria table is the description.
    description = section.split("**Acceptance criteria**")[0].strip()

    lines = [description, "", "## Acceptance criteria", ""]
    if criteria:
        for row in criteria:
            number, criterion = row[0], row[1]
            check = row[2] if len(row) > 2 else ""
            item = f"- [ ] **{number}.** {criterion}"
            if check:
                item += f"  \n  <sub>Check: {check}</sub>"
            lines.append(item)
    else:
        lines.append("_None recorded; see the story document._")

    lines += [
        "",
        "---",
        "",
        f"Source of record: [`docs/project-plan/stories.md`]"
        f"(../blob/develop/docs/project-plan/stories.md) — {story_id}.",
        "Edit the document, not this issue, when the story itself changes.",
    ]
    return "\n".join(lines)


def create_issue(title, body, dry_run):
    command = ["gh", "issue", "create", "--title", title, "--body", body]
    for label in LABELS:
        command += ["--label", label]
    if MILESTONE:
        command += ["--milestone", MILESTONE]

    if dry_run:
        print(f"  would run: gh issue create --title {title!r} …")
        return True

    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr.strip()}", file=sys.stderr)
        return False
    print(f"  created: {result.stdout.strip()}")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--create", action="store_true",
                        help="actually create the issues (default is a dry run)")
    parser.add_argument("--only", metavar="S-nn",
                        help="import a single story")
    parser.add_argument("--show-body", action="store_true",
                        help="print the full issue body in a dry run")
    arguments = parser.parse_args()

    if not STORIES.exists():
        sys.exit(f"{STORIES} not found; run from the repository root")

    stories = list(parse_stories(STORIES.read_text()))
    if not stories:
        sys.exit("no stories parsed; has the heading format changed?")

    if arguments.create and not shutil_which("gh"):
        sys.exit("gh is not installed. Install it and run `gh auth login`, "
                 "or drop --create to see what would be produced.")

    selected = [s for s in stories if not arguments.only or s[0] == arguments.only]
    if not selected:
        sys.exit(f"no story matching {arguments.only}")

    print(f"{'Creating' if arguments.create else 'Would create'} "
          f"{len(selected)} issue(s) from {STORIES}\n")

    failures = 0
    for story_id, title, section in selected:
        issue_title = f"{story_id} — {title}"
        body = build_body(story_id, section)
        criteria = len(table_rows(section, "criterion"))
        print(f"{issue_title}  ({criteria} acceptance criteria)")
        if arguments.show_body and not arguments.create:
            print("\n" + "\n".join("    " + l for l in body.split("\n")) + "\n")
        if not create_issue(issue_title, body, dry_run=not arguments.create):
            failures += 1

    if failures:
        sys.exit(f"\n{failures} issue(s) failed")
    if not arguments.create:
        print("\nDry run. Re-run with --create to create these on GitHub.")


def shutil_which(name):
    import shutil
    return shutil.which(name)


if __name__ == "__main__":
    main()
