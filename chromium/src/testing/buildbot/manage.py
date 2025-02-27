#!/usr/bin/env python
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Toolbox to manage all the json files in this directory.

It can reformat them in their canonical format or ensures they are well
formatted.
"""

import argparse
import ast
import collections
import glob
import json
import os
import subprocess
import sys


THIS_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.dirname(os.path.dirname(THIS_DIR))
sys.path.insert(0, os.path.join(SRC_DIR, 'third_party', 'colorama', 'src'))

import colorama


SKIP = {
  # These are not 'builders'.
  'compile_targets', 'gtest_tests', 'filter_compile_builders',
  'non_filter_builders', 'non_filter_tests_builders',

  # These are not supported on Swarming yet.
  # http://crbug.com/472205
  'Chromium Mac 10.10',
  # http://crbug.com/441429
  'Linux Trusty (32)', 'Linux Trusty (dbg)(32)',

  # http://crbug.com/480053
  'Linux GN',
  'Linux GN (dbg)',

  # Unmaintained builders on chromium.fyi
  'ClangToTMac',
  'ClangToTMacASan',

  # This builder is fine, but win8_chromium_ng uses GN and this configuration,
  # which breaks everything.
  'Win8 Aura',

  # One off builders. Note that Swarming does support ARM.
  'Linux ARM Cross-Compile',
  'Site Isolation Linux',
  'Site Isolation Win',
}


SKIP_GN_ISOLATE_MAP_TARGETS = {
  # TODO(GYP): These targets have not been ported to GN yet.
  'cast_media_unittests',
  'cast_shell_browser_test',
  'chromevox_tests',
  'nacl_helper_nonsfi_unittests',

  # These targets are run on the bots but not listed in the
  # buildbot JSON files.
  'angle_end2end_tests',
  'content_gl_tests',
  'gl_tests',
  'gles2_conform_test',
  'tab_capture_end2end_tests',
  'telemetry_gpu_test',
}


class Error(Exception):
  """Processing error."""


def get_isolates():
  """Returns the list of all isolate files."""
  files = subprocess.check_output(['git', 'ls-files'], cwd=SRC_DIR).splitlines()
  return [os.path.basename(f) for f in files if f.endswith('.isolate')]


def process_builder_convert(data, test_name):
  """Converts 'test_name' to run on Swarming in 'data'.

  Returns True if 'test_name' was found.
  """
  result = False
  for test in data['gtest_tests']:
    if test['test'] != test_name:
      continue
    test.setdefault('swarming', {})
    if not test['swarming'].get('can_use_on_swarming_builders'):
      test['swarming']['can_use_on_swarming_builders'] = True
    result = True
  return result


def process_builder_remaining(data, filename, builder, tests_location):
  """Calculates tests_location when mode is --remaining."""
  for test in data['gtest_tests']:
    name = test['test']
    if test.get('swarming', {}).get('can_use_on_swarming_builders'):
      tests_location[name]['count_run_on_swarming'] += 1
    else:
      tests_location[name]['count_run_local'] += 1
      tests_location[name]['local_configs'].setdefault(
          filename, []).append(builder)


def process_file(mode, test_name, tests_location, filepath, ninja_targets,
                 ninja_targets_seen):
  """Processes a json file describing what tests should be run for each recipe.

  The action depends on mode. Updates tests_location.

  Return False if the process exit code should be 1.
  """
  filename = os.path.basename(filepath)
  with open(filepath) as f:
    content = f.read()
  try:
    config = json.loads(content)
  except ValueError as e:
    raise Error('Exception raised while checking %s: %s' % (filepath, e))

  for builder, data in sorted(config.iteritems()):
    if builder in SKIP:
      # Oddities.
      continue
    if not isinstance(data, dict):
      raise Error('%s: %s is broken: %s' % (filename, builder, data))
    if 'gtest_tests' not in data:
      continue
    if not isinstance(data['gtest_tests'], list):
      raise Error(
          '%s: %s is broken: %s' % (filename, builder, data['gtest_tests']))
    if not all(isinstance(g, dict) for g in data['gtest_tests']):
      raise Error(
          '%s: %s is broken: %s' % (filename, builder, data['gtest_tests']))

    for d in data['gtest_tests']:
      if (d['test'] not in ninja_targets and
          d['test'] not in SKIP_GN_ISOLATE_MAP_TARGETS):
        raise Error('%s: %s / %s is not listed in gn_isolate_map.pyl.' %
                    (filename, builder, d['test']))
      elif d['test'] in ninja_targets:
        ninja_targets_seen.add(d['test'])

    config[builder]['gtest_tests'] = sorted(
        data['gtest_tests'], key=lambda x: x['test'])

    # The trick here is that process_builder_remaining() is called before
    # process_builder_convert() so tests_location can be used to know how many
    # tests were converted.
    if mode in ('convert', 'remaining'):
      process_builder_remaining(data, filename, builder, tests_location)
    if mode == 'convert':
      process_builder_convert(data, test_name)

  expected = json.dumps(
      config, sort_keys=True, indent=2, separators=(',', ': ')) + '\n'
  if content != expected:
    if mode in ('convert', 'write'):
      with open(filepath, 'wb') as f:
        f.write(expected)
      if mode == 'write':
        print('Updated %s' % filename)
    else:
      print('%s is not in canonical format' % filename)
      print('run `testing/buildbot/manage.py -w` to fix')
    return mode != 'check'
  return True


def print_convert(test_name, tests_location):
  """Prints statistics for a test being converted for use in a CL description.
  """
  data = tests_location[test_name]
  print('Convert %s to run exclusively on Swarming' % test_name)
  print('')
  print('%d configs already ran on Swarming' % data['count_run_on_swarming'])
  print('%d used to run locally and were converted:' % data['count_run_local'])
  for master, builders in sorted(data['local_configs'].iteritems()):
    for builder in builders:
      print('- %s: %s' % (master, builder))
  print('')
  print('Ran:')
  print('  ./manage.py --convert %s' % test_name)
  print('')
  print('R=')
  print('BUG=98637')


def print_remaining(test_name, tests_location):
  """Prints a visual summary of what tests are yet to be converted to run on
  Swarming.
  """
  if test_name:
    if test_name not in tests_location:
      raise Error('Unknown test %s' % test_name)
    for config, builders in sorted(
        tests_location[test_name]['local_configs'].iteritems()):
      print('%s:' % config)
      for builder in sorted(builders):
        print('  %s' % builder)
    return

  isolates = get_isolates()
  l = max(map(len, tests_location))
  print('%-*s%sLocal       %sSwarming  %sMissing isolate' %
      (l, 'Test', colorama.Fore.RED, colorama.Fore.GREEN,
        colorama.Fore.MAGENTA))
  total_local = 0
  total_swarming = 0
  for name, location in sorted(tests_location.iteritems()):
    if not location['count_run_on_swarming']:
      c = colorama.Fore.RED
    elif location['count_run_local']:
      c = colorama.Fore.YELLOW
    else:
      c = colorama.Fore.GREEN
    total_local += location['count_run_local']
    total_swarming += location['count_run_on_swarming']
    missing_isolate = ''
    if name + '.isolate' not in isolates:
      missing_isolate = colorama.Fore.MAGENTA + '*'
    print('%s%-*s %4d           %4d    %s' %
        (c, l, name, location['count_run_local'],
          location['count_run_on_swarming'], missing_isolate))

  total = total_local + total_swarming
  p_local = 100. * total_local / total
  p_swarming = 100. * total_swarming / total
  print('%s%-*s %4d (%4.1f%%)   %4d (%4.1f%%)' %
      (colorama.Fore.WHITE, l, 'Total:', total_local, p_local,
        total_swarming, p_swarming))
  print('%-*s                %4d' % (l, 'Total executions:', total))


def main():
  colorama.init()
  parser = argparse.ArgumentParser(description=sys.modules[__name__].__doc__)
  group = parser.add_mutually_exclusive_group(required=True)
  group.add_argument(
      '-c', '--check', dest='mode', action='store_const', const='check',
      default='check', help='Only check the files')
  group.add_argument(
      '--convert', dest='mode', action='store_const', const='convert',
      help='Convert a test to run on Swarming everywhere')
  group.add_argument(
      '--remaining', dest='mode', action='store_const', const='remaining',
      help='Count the number of tests not yet running on Swarming')
  group.add_argument(
      '-w', '--write', dest='mode', action='store_const', const='write',
      help='Rewrite the files')
  parser.add_argument(
      'test_name', nargs='?',
      help='The test name to print which configs to update; only to be used '
           'with --remaining')
  args = parser.parse_args()

  if args.mode == 'convert':
    if not args.test_name:
      parser.error('A test name is required with --convert')
    if args.test_name + '.isolate' not in get_isolates():
      parser.error('Create %s.isolate first' % args.test_name)

  # Stats when running in --remaining mode;
  tests_location = collections.defaultdict(
      lambda: {
        'count_run_local': 0, 'count_run_on_swarming': 0, 'local_configs': {}
      })

  with open(os.path.join(THIS_DIR, "gn_isolate_map.pyl")) as fp:
    gn_isolate_map = ast.literal_eval(fp.read())
    ninja_targets = {k: v['label'] for k, v in gn_isolate_map.items()}

  try:
    result = 0
    ninja_targets_seen = set()
    for filepath in glob.glob(os.path.join(THIS_DIR, '*.json')):
      if not process_file(args.mode, args.test_name, tests_location, filepath,
                          ninja_targets, ninja_targets_seen):
        result = 1

    extra_targets = (set(ninja_targets) - ninja_targets_seen -
                     SKIP_GN_ISOLATE_MAP_TARGETS)
    if extra_targets:
      if len(extra_targets) > 1:
        extra_targets_str = ', '.join(extra_targets) + ' are'
      else:
        extra_targets_str = list(extra_targets)[0] + ' is'
      raise Error('%s listed in gn_isolate_map.pyl but not in any .json '
                  'files' % extra_targets_str)

    if args.mode == 'convert':
      print_convert(args.test_name, tests_location)
    elif args.mode == 'remaining':
      print_remaining(args.test_name, tests_location)
    return result
  except Error as e:
    sys.stderr.write('%s\n' % e)
    return 1


if __name__ == "__main__":
  sys.exit(main())
