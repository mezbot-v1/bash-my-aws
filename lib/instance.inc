# instance.inc
#
# List, run, start, stop and ssh to Amazon AWS EC2 instances

source $(dirname ${BASH_SOURCE[0]})/shared.inc

instances() {
  # type: query
  # returns instance id's and the name tag.
  local inputs=$(__bma_read_inputs $@)

  if __bma_read_switches ${inputs} | grep ^--debug > /dev/null; then
    BMA_DEBUG=true
  else
    BMA_DEBUG=false
  fi

  local default_query='
    Reservations[].Instances[][
      {
        "InstanceId": InstanceId,
        "Name":       [Tags[?Key==`Name`].Value][0][0]
      }
    ][]
  '

  local instance_ids=$(__bma_read_resources $inputs)
  local filters=$(__bma_read_switches $inputs | grep ^--filters | cut -d\  -f2-)
  local query=$(__bma_read_switches $inputs | grep ^--query | cut -d\  -f2-)
  local output=$(__bma_read_switches $inputs | grep ^--output | cut -d\  -f2-)

  if ${BMA_DEBUG}; then
    echo "inputs: ${inputs}"
    echo "instance_ids: ${instance_ids}"
    echo "filters: ${filters}"
    echo "query: ${query}"
    echo "output: ${output}"
  fi

  [[ -z $query ]] && query=$default_query

  aws ec2 describe-instances                                      \
    $(for x in ${instance_ids}; do echo "--instance-id $x"; done) \
    $([[ -n ${filters} ]] && echo "--filters ${filters}")         \
    --query $(__bma_clean_query ${query})                         \
    --output ${output:-"text"}
}

instance_asg() {
  local inputs=$(__bma_read_inputs $@)

  local query='
    Reservations[].Instances[][
      {
        "AutoscalingGroupName": [Tags[?Key==`aws:autoscaling:groupName`].Value][0][0],
        "InstanceId": InstanceId
      }
    ][]
  '
  instances ${inputs} --query ${query}
}

instance_stack() {
  # FIXME: the output is out of order.
  # for some reason, InstanceId is first in text output.
  local inputs=$(__bma_read_inputs $@)
  local query='
    Reservations[].Instances[][
      {
        "Stack": [Tags[?Key==`aws:cloudformation:stack-name`].Value][0][0],
        "InstanceId": InstanceId
      }
    ][]
  '
  instances ${inputs} --query ${query}
}

instance_type() {
  # FIXME: the output is out of order.
  # for some reason, InstanceId is first in text output.
  local inputs=$(__bma_read_inputs $@)
  local query='
    Reservations[].Instances[][
      {
        "InstanceType": InstanceType,
        "InstanceId": InstanceId
      }
    ][]
  '
  instances ${inputs} --query ${query}
}

instance_role() {
  local inputs=$(__bma_read_inputs $@)

  local query='
    Reservations[].Instances[][
      {
        "AutoscalingGroupName": [Tags[?Key==`aws:autoscaling:groupName`].Value][0][0],
        "InstanceId": InstanceId
      }
    ][]
  '
  instances ${inputs} --query ${query}



  if [ -z "$1" ] ; then echo "Usage: $FUNCNAME instance_id"; return 1; fi
  local instance_id=$1
  local profile_id=$(aws ec2 describe-instances --instance-id $instance_id \
    --query "Reservations[].Instances[].IamInstanceProfile.Id"             \
    --output text)
  aws iam list-instance-profiles                                                     \
    --query "InstanceProfiles[?InstanceProfileId==\`$profile_id\`].Roles[].RoleName" \
    --output text
}


instance_console_output() {
  # detail
  for instance_id in $(__bma_read_inputs $@); do
    aws ec2 get-console-output    \
      --instance-id $instance_id  \
      --query Output              \
      --output text
  done
}

instance_iam_profile() {
  # detail
  instance_ids=$(__bma_read_inputs $@)
  INSTANCE_OUTPUT="[ InstanceId IamInstanceProfile.Id ]" \
    instances $instance_ids
}

instance_ssh() {
  IFS=' ' read instance_id user trash <<< $(__bma_read_inputs $@ | head -1)
  if [ -z "$instance_id" ] ; then echo "Usage: $FUNCNAME instance-id [user]"; return 1; fi
  local instance_details="$(instance_ssh_details $instance_id)"
  local keyname=$(echo $instance_details | awk '{print $1}');
  local ip=$(echo $instance_details | awk '{print $2}');
  local instance_name=$(echo $instance_details | awk '{print $3}');
  local instance_default_user=$(echo $instance_details | awk '{print $4}');

  local USERNAME=${user:-${instance_default_user:-${AWS_DEFAULT_USER:-root}}}
  echo "Connecting to $instance_id $instance_name"

  ssh                               \
    -tt                             \
    -i ~/.ssh/$keyname              \
    -o LogLevel=error               \
    -o StrictHostKeyChecking=no     \
    -o UserKnownHostsFile=/dev/null \
    -l $USERNAME                    \
    $ip
}

instance_ssh_details() {
  local instance_id="$(__bma_read_inputs $@)"
  if [ -z "$instance_id" ] ; then echo "Usage: $FUNCNAME instance_id"; return 1; fi
  INSTANCE_FILTERS="Name=instance-id,Values=${instance_id}"           \
  INSTANCE_OUTPUT="[
      KeyName,
      PrivateIpAddress,
      join(\` \`, [Tags[?Key==\`Name\`].Value][] || [\`not-named\`]),
      join(\` \`, [Tags[?Key==\`default-user\`].Value][] || [\`\`])
  ]" instances
}

instance_start() {
  if [ -z "$1" ] ; then echo "Usage: $FUNCNAME instance-id"; return 1; fi
  aws ec2 start-instances --instance-id $1
}

instance_stop() {
  if [ -z "$1" ] ; then echo "Usage: $FUNCNAME instance-id"; return 1; fi
  aws ec2 stop-instances --instance-id $1
}

instance_terminate() {
  aws ec2 modify-instance-attribute --attribute disableApiTermination --value false --instance-id $1
  aws ec2 terminate-instances --instance-id $1
}

instance_userdata() {
  # detail
  for instance_id in $(__bma_read_inputs $@); do
    aws ec2 describe-instance-attribute         \
      --attribute userData                      \
      --instance-id $instance_id                \
      --query UserData                          \
      --output text                             |
        base64 --decode
  done
}

instance_volumes() {
  # detail
  # TODO: fix the output.
  INSTANCE_OUTPUT="BlockDeviceMappings[].Ebs[].VolumeId" \
    instances $(__bma_read_inputs $@)
}

# instance_volumes() {
#   if [ -z "$1" ] ; then echo "Usage: $FUNCNAME instance_id [instance_id]"; return 1; fi
#   local instance_ids=$@
#   if local volume_ids=$(aws ec2 describe-instances --instance-ids $instance_ids \
#    --query "Reservations[].Instances[].BlockDeviceMappings[].Ebs[].VolumeId"    \
#    --output text)
#   then
#     aws ec2 describe-volumes --volume-ids $volume_ids \
#       --query "Volumes[].$VOLUME_OUTPUT"              \
#       --output text
#   else
#     >&2 echo "No volumes found"
#   fi
# }

# vim: ft=sh