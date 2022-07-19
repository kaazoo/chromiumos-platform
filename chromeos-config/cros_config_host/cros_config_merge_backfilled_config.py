#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A command-line utility to merge from two ConfigBundle protos. One
ConfigBundle is generated from Starlark source files, another is generated from
external data sources such as HWID and model.yaml (i.e. "backfilled").

For each repeated field in ConfigBundle, all elements in
the backfilled ConfigBundle that are not in the original ConfigBundle are added
to the original ConfigBundle. The id field of each message is used to determine
equality.

For example, in the components repeated field, each Component's id.value
field is used to determine whether to add it to config_bundle.
"""

import argparse
import pathlib
from typing import Callable, Sequence, TypeVar

from chromiumos.config.payload import config_bundle_pb2
from google.protobuf import json_format


def _parse_config_bundle(path: pathlib.Path) -> config_bundle_pb2.ConfigBundle:
    cb = config_bundle_pb2.ConfigBundle()
    json_format.Parse(path.read_text(), cb)
    return cb


M = TypeVar("M")


def _merge_sequences_by_key(
    seq1: Sequence[M], seq2: Sequence[M], keyFn: Callable[[M], str]
):
    """Merges two sequences containing the same type of objects.

    For each element in seq2, if seq1 does not contain the element, as
    determined by keyFn, the element is added to seq1. If both sequences contain
    an element, it is not added to seq1.

    Args:
        seq1: The sequence to be modified.
        seq2: The sequence whose elements will be added to seq1.
        keyFn: A function to map from the object type to strings.
    """
    seq1Keys = set()
    for elem in seq1:
        seq1Keys.add(keyFn(elem))

    for elem in seq2:
        if keyFn(elem) not in seq1Keys:
            seq1.append(elem)


def merge_config_bundles(
    config_bundle: config_bundle_pb2.ConfigBundle,
    backfilled_config_bundle: config_bundle_pb2.ConfigBundle,
):
    """Merges backfilled_config_bundle into config_bundle.

    For each repeated field in ConfigBundle, all elements in
    backfilled_config_bundle that are not in config_bundle are added to
    config_bundle. The id field of each message is used to determine equality.

    For example, in the components repeated field, each Component's id.value
    field is used to determine whether to add it to config_bundle.

    Args:
        config_bundle: Original ConfigBundle to merge into.
        backfilled_config_bundle: ConfigBundle generated by the backfiller to
            merge elements from.
    """
    _merge_sequences_by_key(
        config_bundle.partner_list,
        backfilled_config_bundle.partner_list,
        lambda p: p.id.value,
    )
    _merge_sequences_by_key(
        config_bundle.components,
        backfilled_config_bundle.components,
        lambda c: c.id.value,
    )
    _merge_sequences_by_key(
        config_bundle.program_list,
        backfilled_config_bundle.program_list,
        lambda p: p.id.value,
    )
    _merge_sequences_by_key(
        config_bundle.design_list,
        backfilled_config_bundle.design_list,
        lambda d: d.id.value.lower(),
    )
    _merge_sequences_by_key(
        config_bundle.device_brand_list,
        backfilled_config_bundle.device_brand_list,
        lambda db: db.id.value,
    )
    _merge_sequences_by_key(
        config_bundle.software_configs,
        backfilled_config_bundle.software_configs,
        lambda s: s.design_config_id.value,
    )
    _merge_sequences_by_key(
        config_bundle.brand_configs,
        backfilled_config_bundle.brand_configs,
        lambda b: b.brand_id.value,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config-bundle",
        type=pathlib.Path,
        required=True,
        help=(
            "Path to a ConfigBundle generated from Starlark source files, "
            "in jsonpb format."
        ),
    )
    parser.add_argument(
        "--backfilled-config-bundle",
        type=pathlib.Path,
        required=True,
        help=(
            "Path to a ConfigBundle generated from external data sources, "
            'i.e. the "backfilled" ConfigBundle, in jsonpb format.'
        ),
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        required=True,
        help="Path to write the merged ConfigBundle.",
    )

    args = parser.parse_args()
    config_bundle = _parse_config_bundle(args.config_bundle)
    backfilled_config_bundle = _parse_config_bundle(
        args.backfilled_config_bundle
    )

    merge_config_bundles(config_bundle, backfilled_config_bundle)

    args.output.write_text(
        json_format.MessageToJson(config_bundle, sort_keys=True)
    )


if __name__ == "__main__":
    main()
