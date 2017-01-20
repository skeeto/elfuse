(require 'elfuse-module)
(require 'seq)

(defconst elfuse--time-between-checks 0.01
  "Time interval in seconds between Elfuse request checks.")

(defvar elfuse--check-timer nil
  "Timer calling the callback-responding function")

(defun elfuse-start (mountpath)
  "Start Elfuse using a given MOUNTPATH."
  (interactive "DElfuse mount path: ")
  (if (elfuse--dir-mountable-p mountpath)
      (let ((abspath (file-truename mountpath)))
	(elfuse--start-loop)
	(elfuse--mount abspath)
        (add-hook 'kill-emacs-hook 'elfuse--stop))
    (message "Elfuse: %s does not exist or is not empty." mountpath)))

(defun elfuse-stop ()
  "Stop Elfuse."
  (interactive)
  (elfuse--stop)
  (remove-hook 'kill-emacs-hook 'elfuse--stop))

(defmacro elfuse-define-op (opname arglist &rest body)
  "Define a Fuse operation OPNAME handler."
  (declare (indent 2))
  `(defun ,(intern (concat "elfuse--" (symbol-name opname) "-callback"))
       ,arglist
     ,@body))

(defun elfuse--start-loop ()
  (setq elfuse--check-timer
        (run-at-time nil elfuse--time-between-checks 'elfuse--on-timer)))

(defun elfuse--on-timer ()
  (unless (elfuse--check-callbacks)
    (cancel-timer timer)
    (setq elfuse--check-timer nil)))

(defun elfuse--dir-mountable-p (path)
  (and (file-exists-p path)
       ;; only . and ..
       (= (length (directory-files path)) 2)))

(provide 'elfuse)