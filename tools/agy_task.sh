#!/bin/sh
# Delegate a simple, bulky task to Antigravity CLI (agy, Gemini Flash) in an isolated task directory.
#
#   tools/agy_task.sh TASKDIR PROMPT_FILE [MODEL] [TIMEOUT]
#
# TASKDIR must already contain only the inputs the task needs (copies, never the project tree itself);
# agy works with cwd = TASKDIR/work. The prompt, the JSON result (incl. token usage) and agy's log stay in TASKDIR.
# Facts checked on 2026-10-20 (agy print mode, --mode accept-edits): it edits files and runs shell commands
# WITHOUT asking, and --sandbox does NOT stop writes outside the workspace or network access.
# So: inputs are copies, outputs are reviewed and tested before anything is moved into the project.
set -e
dir=$1; prompt=$2; model=${3:-gemini-3.8-flash-medium}; timeout=${4:-1800s}
[ -d "$dir/work" ] || { echo "no $dir/work"; exit 2; }
cp "$prompt" "$dir/prompt.md"
cd "$dir/work"
agy -p "$(cat ../prompt.md)" --model "$model" --mode accept-edits --print-timeout "$timeout" \
    --output-format json --log-file ../agy.log > ../result.json 2> ../stderr.txt || echo "agy exit $?" >> ../stderr.txt
python3 -c "import json,sys; r=json.load(open('../result.json')); u=r.get('usage',{}); print(r.get('status'), '%.0fs' % r.get('duration_seconds',0), 'tokens in/out/think', u.get('input_tokens'), u.get('output_tokens'), u.get('thinking_tokens'))"
