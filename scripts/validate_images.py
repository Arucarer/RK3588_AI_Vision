#!/usr/bin/env python3
"""Prepare PC/RKNN image comparisons; compare deployment agreement, not accuracy."""
import argparse
import csv
import json
import re
from pathlib import Path

NAMES = ['crazing', 'inclusion', 'patches', 'pitted_surface',
         'rolled-in_scale', 'scratches']


def prepare(args):
    import cv2
    import numpy as np
    from ultralytics import YOLO

    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'manifest.json').exists():
        raise SystemExit('Output already contains a manifest; use a new directory.')
    images = out / 'images'
    images.mkdir(exist_ok=True)
    dataset = Path(args.dataset)
    selected = {}
    for label in sorted((dataset / 'labels/val').glob('*.txt')):
        rows = [line.split() for line in label.read_text().splitlines() if line.strip()]
        if not rows:
            continue
        classes = {int(row[0]) for row in rows}
        source = dataset / 'images/val' / (label.stem + '.jpg')
        if source.exists():
            selected[source] = {'classes': classes, 'gt_count': len(rows)}
    sources = set()
    for c in range(6):
        eligible = [p for p, meta in selected.items() if c in meta['classes']]
        # Ensure each class's first selected case has multiple GT boxes if available.
        eligible.sort(key=lambda p: (selected[p]['gt_count'] < 2, p.name))
        if len(eligible) < 3:
            raise SystemExit('Not enough validation samples for class %d' % c)
        sources.update(eligible[:3])

    cases = []
    for source in sorted(sources):
        bgr = cv2.imread(str(source))
        if bgr is None:
            raise SystemExit('Cannot decode ' + str(source))
        h, w = bgr.shape[:2]
        variants = [('original', bgr, 0, 0)]
        # One source per class: synthetic portrait/landscape canvases.
        # PNG is used so both decoders receive identical pixels.
        for c in sorted(selected[source]['classes']):
            candidates = sorted(p for p in sources if c in selected[p]['classes'])
            if source == candidates[0]:
                landscape = np.full((h, w * 2, 3), 114, dtype=np.uint8)
                landscape[:, w // 2:w // 2 + w] = bgr
                portrait = np.full((h * 2, w, 3), 114, dtype=np.uint8)
                portrait[h // 2:h // 2 + h, :] = bgr
                variants.extend([('landscape', landscape, w // 2, 0),
                                 ('portrait', portrait, 0, h // 2)])
                break
        for variant, pixels, dx, dy in variants:
            name = source.stem + '__' + variant + '.png'
            if not cv2.imwrite(str(images / name), pixels):
                raise SystemExit('Cannot write ' + name)
            cases.append({'name': name, 'source': str(source),
                          'variant': variant, 'offset': [dx, dy],
                          'width': pixels.shape[1], 'height': pixels.shape[0],
                          'gt_count_source': selected[source]['gt_count'],
                          'classes_source': sorted(selected[source]['classes'])})

    model = YOLO(args.weights)
    names = [model.names[i] for i in range(6)]
    if names != NAMES:
        raise SystemExit('Unexpected class order: ' + repr(names))
    predictions = {}
    for case in cases:
        result = model.predict(source=str(images / case['name']), imgsz=640,
                               rect=False, conf=0.25, iou=0.45, max_det=100,
                               agnostic_nms=False, device='cpu', verbose=False)[0]
        predictions[case['name']] = [
            {'class_id': int(b.cls.item()), 'confidence': float(b.conf.item()),
             'xyxy': b.xyxy[0].tolist()} for b in result.boxes]
        print(case['name'], 'PC boxes=', len(result.boxes), flush=True)
    (out / 'pc.json').write_text(json.dumps(predictions, indent=2))
    (out / 'manifest.json').write_text(json.dumps(
        {'weights': str(Path(args.weights).resolve()), 'cases': cases,
         'settings': {'imgsz': 640, 'conf': 0.25, 'iou': 0.45, 'max_det': 100}},
        indent=2))
    (out / 'run_board.sh').write_text('''#!/bin/sh
set -eu
cd "$(dirname "$0")"
APP=${1:-/opt/rk3588_ai/rk3588_ai_detect}
MODEL=${2:-/opt/rk3588_ai/model/best_fp16.rknn}
mkdir -p board_logs
for img in images/*.png; do
    name=${img##*/}
    echo "Testing $name"
    code=0
    if command -v timeout >/dev/null 2>&1; then
        timeout 60 "$APP" --image "$MODEL" "$img" >"board_logs/$name.log" 2>&1 || code=$?
    else
        "$APP" --image "$MODEL" "$img" >"board_logs/$name.log" 2>&1 || code=$?
    fi
    echo "$code" >"board_logs/$name.status"
done
echo "Finished: board_logs"
''')
    print('Prepared %d cases at %s' % (len(cases), out), flush=True)


def iou(a, b):
    area = lambda r: max(0, r[2]-r[0]) * max(0, r[3]-r[1])
    inter = max(0, min(a[2], b[2])-max(a[0], b[0])) * max(0, min(a[3], b[3])-max(a[1], b[1]))
    union = area(a) + area(b) - inter
    return inter / union if union > 0 else 0.0


def compare(args):
    import math
    import numpy as np
    from scipy.optimize import linear_sum_assignment

    root = Path(args.directory)
    manifest = json.loads((root / 'manifest.json').read_text())
    pc = json.loads((root / 'pc.json').read_text())
    reports = []
    for case in manifest['cases']:
        name = case['name']
        entry = {'image': name, 'status': 'ERROR', 'pc_boxes': len(pc[name])}
        try:
            status = (root / 'board_logs' / (name + '.status')).read_text().strip()
            text = (root / 'board_logs' / (name + '.log')).read_text(errors='replace')
            success = re.search(r'image inference success: boxes=(\d+)', text)
            if status != '0' or success is None:
                raise ValueError('board exit=%s or success marker missing' % status)
            board = []
            for row in csv.reader(text.splitlines()):
                if len(row) == 7 and row[0].isdigit():
                    c = int(row[0])
                    values = [float(v) for v in row[2:]]
                    if c not in range(6) or row[1] != NAMES[c] or not all(map(math.isfinite, values)):
                        raise ValueError('Invalid prediction row')
                    board.append({'class_id': c, 'confidence': values[0], 'xyxy': values[1:]})
            if len(board) != int(success.group(1)):
                raise ValueError('Printed box count mismatch')
            reference = pc[name]
            matches = []
            if reference and board:
                cost = np.zeros((len(reference), len(board)))
                for i, a in enumerate(reference):
                    for j, b in enumerate(board):
                        overlap = iou(a['xyxy'], b['xyxy'])
                        if a['class_id'] == b['class_id'] and overlap >= 0.5:
                            cost[i, j] = -1000 - overlap
                rows, cols = linear_sum_assignment(cost)
                matches = [(int(i), int(j)) for i, j in zip(rows, cols) if cost[i, j] < 0]
            pair_metrics = [
                {'pc_index': i, 'board_index': j,
                 'iou': iou(reference[i]['xyxy'], board[j]['xyxy']),
                 'score_delta': abs(reference[i]['confidence']-board[j]['confidence']),
                 'max_coord_delta': max(abs(a-b) for a, b in zip(reference[i]['xyxy'], board[j]['xyxy']))}
                for i, j in matches]
            agrees = len(matches) == len(reference) == len(board) and all(
                m['iou'] >= 0.9 and m['score_delta'] <= 0.05 for m in pair_metrics)
            entry.update(status=('BOTH_EMPTY' if not reference and not board else
                                 'AGREE' if agrees else 'REVIEW'),
                         board_boxes=len(board), matches=pair_metrics,
                         unmatched_pc=[i for i in range(len(reference)) if i not in {m[0] for m in matches}],
                         unmatched_board=[j for j in range(len(board)) if j not in {m[1] for m in matches}],
                         board_predictions=board)
        except (OSError, ValueError) as error:
            entry['error'] = str(error)
        reports.append(entry)
        print(name, entry['status'], 'PC=', entry['pc_boxes'], 'board=', entry.get('board_boxes', '?'))
    (root / 'comparison.json').write_text(json.dumps(reports, indent=2))
    for status in ['AGREE', 'BOTH_EMPTY', 'REVIEW', 'ERROR']:
        print(status, sum(r['status'] == status for r in reports))
    print('Saved comparison.json. Agreement is not ground-truth detection accuracy.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--dataset', default='/home/aaa/rk3588_ai/datasets/defect')
    p.add_argument('--weights', default='/home/aaa/rk3588_ai/output/neu_yolov8n_test/weights/best.pt')
    p.add_argument('--output', required=True)
    p.set_defaults(func=prepare)
    p = sub.add_parser('compare')
    p.add_argument('directory')
    p.set_defaults(func=compare)
    args = parser.parse_args()
    args.func(args)
