#!/usr/bin/python3
#
# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES.
# All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

import re
import os
import sys
import shutil
import argparse

VERSION = "1.0"


class SystemLogParser:

    class Column:
        EUID = 0
        MSG = 5
        NUM_COLUMNS = 5

    def __init__(self, args):

        # 2GB approx assuming avg log size is 256 bytes
        self.max_log_count = 8388608

        self.inter_folder_path = "./temp_nvlog"

        self.input_file = args.input_file
        self.output_file = args.output_file
        self.start_token = args.start_token
        self.end_token = args.end_token
        self.writetype = args.writetype
        print("Input file - ", self.input_file)
        print("Output file - ", self.output_file)
        print("Start token - ", self.start_token)
        print("End token - ", self.end_token)

        self.infile_p = None
        self.outfile_p = None
        self.inter_file_p = None

    def print_error_and_exit(self, msg, exit_code=1):
        print("Error: " + msg, file=sys. stderr)
        self.cleanUp()
        sys.exit(exit_code)

    def clean_up(self):
        if self.infile_p is not None:
            self.infile_p.close()
        if self.infile_p is not None:
            self.outfile_p.close()
        try:
            if os.path.exists(self.inter_folder_path):
                shutil.rmtree(self.inter_folder_path)
        except OSError as o:
            print(f"Error, {o.strerror}: {self.inter_folder_path}")

    def parse_input(self):
        ######################################################################
        #      Reading/parsing the input file provided                       #
        ######################################################################
        line_count = 0
        log_count = 0
        previous_ts = 0
        filename_i = 0
        log_dictionary = {}
        file_count = 0
        try:
            self.infile_p = open(self.input_file, "r", encoding="utf-8")
        except OSError as e:
            self.print_error_and_exit(f"{e}")
        try:
            self.outfile_p = open(self.output_file, self.writetype,
                                  encoding="utf-8")
        except OSError as e:
            self.print_error_and_exit(f"{e}")
        if not os.path.exists(self.inter_folder_path):
            os.makedirs(self.inter_folder_path)
        inter_file = self.inter_folder_path + "/" + str(filename_i) + ".log"
        try:
            self.inter_file_p = open(inter_file, "w", encoding="utf-8")
        except OSError as e:
            self.print_error_and_exit(f"{e}")

        logs_per_file = self.max_log_count
        regex_exp = self.start_token + r"\d+" + self.end_token

        for line in self.infile_p:
            line_count += 1
            if line_count == 1:
                # Ignore header of log file
                continue
            msg_partition = line.split(None,
                                       SystemLogParser.Column.NUM_COLUMNS)
            if len(msg_partition) <= SystemLogParser.Column.NUM_COLUMNS:
                continue
            euid = msg_partition[SystemLogParser.Column.EUID]
            msg = self.decode_message(
                msg_partition[SystemLogParser.Column.MSG])
            index_o = re.search(regex_exp, msg)  # index object
            if index_o is not None:
                temp_substr = msg[index_o.span()[0]:index_o.span()[1]]
                index_ts = re.search(r"\d+", temp_substr)
                time_stamp = temp_substr[index_ts.span()[0]:index_ts.span()[1]]
                final_str = euid + "\t" + time_stamp + "\t" + msg
                previous_ts = int(time_stamp)
                if previous_ts in log_dictionary:
                    temp_Val = log_dictionary.get(previous_ts)
                    temp_Val.append(final_str)
                    log_dictionary.update({previous_ts: temp_Val})
                else:
                    log_dictionary[previous_ts] = [final_str]
            else:
                final_str = euid + "\t" + "          \t" + msg
                if previous_ts in log_dictionary:
                    temp_Val = log_dictionary[previous_ts]
                    temp_Val.append(final_str)
                    log_dictionary.update({previous_ts: temp_Val})
                else:
                    log_dictionary[previous_ts] = [final_str]

            log_count += 1
            if log_count == self.max_log_count:
                Keys = list(log_dictionary.keys())
                Keys.sort()
                sorted_dict = {key: log_dictionary[key] for key in Keys}
                for key in sorted_dict:
                    for value in sorted_dict.get(key):
                        self.inter_file_p.write(value)
                log_dictionary.clear()
                sorted_dict.clear()
                log_count = 0
                self.inter_file_p.close()
                file_count += 1
                filename_i += 1
                inter_file = self.inter_folder_path + "/" + str(
                    filename_i) + ".log"
                try:
                    self.inter_file_p = open(inter_file, "w", encoding="utf-8")
                except OSError as e:
                    self.print_error_and_exit(f"{e}")

        if log_count < self.max_log_count:
            Keys = list(log_dictionary.keys())
            Keys.sort()
            sorted_dict = {key: log_dictionary[key] for key in Keys}
            for key in sorted_dict:
                for value in sorted_dict.get(key):
                    self.inter_file_p.write(value)
            log_dictionary.clear()
            sorted_dict.clear()
            self.inter_file_p.close()
            file_count += 1

        if file_count == 1:
            shutil.move(inter_file, self.output_file)

        self.clean_up()

    def parse_foundation_server_args(self, msg):
        if re.search("_server_native", msg):
            if re.search("Event nvlog:", msg):
                i = 1
                while 1:
                    arg = re.search(" arg" + str(i) + "=[0-9a-fA-F]+", msg)
                    if arg:
                        data = bytes.fromhex(
                            arg.group().split("=")[1])[::-1].decode('utf-8')
                        msg = re.sub(arg.group(), data, msg)
                        i += 1
                    else:
                        break
                msg = re.sub("\x00", "", msg)
                msg = msg.rstrip() + "\n"
        return msg

    def decode_message(self, msg):
        msg = self.parse_foundation_server_args(msg)
        return msg


def main():
    parser = argparse.ArgumentParser(
        description="This script parses slog2/syslog file and provides "
        "output in nvlog file format")
    parser.add_argument("-v", "--version", action="version",
                        version="%(prog)s {}".format(VERSION))
    parser.add_argument("-in", "--input_file", required=True,
                        help="input file")
    parser.add_argument("-out", "--output_file", required=True,
                        help="output file")
    parser.add_argument("-st", "--start_token", default=r'\[',
                        help="start token (Default - [ ). Note : Use '\\' in "
                        "front of token character [,],{,},(,)")
    parser.add_argument("-et", "--end_token", default=r'\]',
                        help="end token (Default - ] ). Note : Use '\\' in "
                        "front of token character [,],{,},(,).")
    parser.add_argument("-wt", "--writetype", default='w',
                        help="Write type in output file. "
                        "'w' (Default value) overwrites output file. "
                        "'a' appends to output file.")
    args = parser.parse_args()
    SystemLogParser(args).parse_input()


if __name__ == '__main__':
    main()
