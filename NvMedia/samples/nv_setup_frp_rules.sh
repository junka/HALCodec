#!/bin/bash
# Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

#
# nv_setup_frp_rules.sh - script to setup FRP rules for the MAC.
#							Takes cfg file as argument, The script
#							uses sample application(nvether_sample_app)
#							in current dir to setup the rules
#

echo "Starting script to setup FRP rules"

cfg_file=$1

#Check if config file exists
if [ ! -f "$cfg_file" ]; then
	echo "\'$cfg_file\' file doesn't exists"
	return 1
fi

setup_frp_rules ()
{
	if [[ "$2" == "frp_add" ]] || [[ "$2" == "frp_update" ]]; then
		INTERFACE=$1
		FRP_CMD=$2
		IDX=$3
		MATCH_DATA=$4
		MATCH_DATA_TYPE=$5
		MODE=$6
		OFFSET=$7
		OKI=$8
		DMA_CH=$9

		./nvether_sample_app $INTERFACE $FRP_CMD $IDX $MATCH_DATA $MATCH_DATA_TYPE $MODE $OFFSET $OKI $DMA_CH
		if [ $? != 0 ]; then
			echo "FRP rule \"./nvether_sample_app $INTERFACE $FRP_CMD $IDX $MATCH_DATA $MATCH_DATA_TYPE $MODE $OFFSET $OKI $DMA_CH\" failed"
			return 1;
		fi
	elif [[ "$2" == "frp_del" ]]; then
		INTERFACE=$1
		FRP_CMD=$2
		IDX=$3

		./nvether_sample_app $INTERFACE $FRP_CMD $IDX
		if [ $? != 0 ]; then
			echo "FRP rule \"./nvether_sample_app $INTERFACE $FRP_CMD $IDX\" failed"
			return 2;
		fi
	else
		echo "Unsupported FRP Cmd : $2"
		return 3
	fi

	return 0
}

while read line;
do
	#Reading each line. Skip blank lines and commented lines
	if [[ -z "$line" ]] || [[ $line == \#* ]]; then
		continue
	else
		setup_frp_rules $line
		if [ $? != 0 ]; then
			echo "Failed to setup FRP rules. Exiting.."
			return 2;
		fi
	fi
done < $cfg_file
