# .bashrc

# Source global definitions
if [ -f /etc/bashrc ]; then
	. /etc/bashrc
fi

# Uncomment the following line if you don't like systemctl's auto-paging feature:
# export SYSTEMD_PAGER=

# User specific aliases and functions
hulftname="[hulft \u@\h \W]\$"
hulftdummy="[dummy \u@\h \W]\$"

alias hstart="hulsndd;hulrcvd;hulobsd;"
alias hstop="utlkillsnd;utlkillrcv;utlkillobs;"
alias seth=". ~/hulft/etc/hulft.bsh.profile;export PS1=\"$hulftname\""
alias setd=". ~/dhulft/etc/hulft.bsh.profile;export PS1=\"$hulftdummy\""
# added by Anaconda3 4.1.1 installer
export PATH="/home/okada/anaconda3/bin:$PATH"
export http_proxy="http://Shinsei_Okada:shu11lin@192.168.236.23:8080"
export https_proxy="https://Shinsei_Okada:shu11lin@192.168.236.23:8080"
export ftp_proxy="ftp://Shinsei_Okada:shu11lin@192.168.236.23:8080"
export HTTP_PROXY="http://Shinsei_Okada:shu11lin@192.168.236.23:8080"
export HTTPS_PROXY="https://Shinsei_Okada:shu11lin@192.168.236.23:8080"
export FTP_PROXY="ftp://Shinsei_Okada:shu11lin@192.168.236.23:8080"

