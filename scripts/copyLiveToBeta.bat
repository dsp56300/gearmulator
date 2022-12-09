set version=%1
rclone copy --transfers=2 --progress -v --include "*%version%*" --max-depth 1 dsp56300_ftp:builds/master/ dsp56300_ftp:builds/master/beta/
