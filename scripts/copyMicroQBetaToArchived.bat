set version=%1
rclone copy --transfers=3 --progress -v --include "*%version%*" --max-depth 1 dsp56300_ftp:builds/microQ/beta/ dsp56300_ftp:builds/microQ/archived/%version%/
