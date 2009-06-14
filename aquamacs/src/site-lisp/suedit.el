(when nil
;; put file name in handler

(add-to-list 'file-name-handler-alist
	     '("/tmp/xxx" . suedit-handler))


(defvar suedit-copies nil)
;; contains list of (file copy auth-code)


(defun suedit-handler (fun &rest args)
  (let ((file-name-handler-alist nil))
  
    ;; is there a copy?
    ;; if so, check to make sure it is 

    (cond
     ((memq fun '(insert-file-contents))
      ;; first arg MUST be filename
      ;; use the copy instead of buffer-file-name
      ;; we assume that the buffer is current.
      (apply fun (cons (suedit-update-copy (car args)) (cdr args))))
     (t
      ;; call emacs process
      ;; maybe convert file name to file-truename??
      (let ((code
	     (with-output-to-string
	       (let ((print-level nil) (print-length nil))
		 (print (cons fun args))))))
	
	(with-temp-buffer
	  (suedit-exec "emacs" (list nil t nil "-batch" "-Q" "-eval" (format "(print %s)" code)))
	  (goto-char (point-min))
	  (read (current-buffer))))
    ))))

;; rally, update the copy
;; but a lot of calls need to be sent to a local "emacs" binary, i.e. AE itself
;; in batch mode
;; a second call (thanks to caching) is going to be fast

;; (file-maybe-accessible-by-root "/tmp/rdir/test")



; go up the directory hierarchy

(defun file-maybe-accessible-by-root (file)
  "Returns non-nil if file may be readable by root."

  ;; we identify the last directory in the path that we see
  ;; exists and check whether it is readable.  If not,
  ;; root should be able to see beyond that, and we
  ;; return `t'.
  (let ((file (file-truename file))
	(dir file)
	(readable-by-root nil))

    (while dir
      (setq dir (directory-file-name (file-name-directory dir)))

      (when (or (file-exists-p dir)
		;; no further expansion possible
		(equal dir (file-name-directory dir)))
		
	(if (not (file-readable-p dir))
	    ;; existing file/dir is not readable:
	    (setq readable-by-root t))
	;; stop iteration
	(setq dir nil)))
    readable-by-root))

 
(defun suedit-update-copy (file)
  (suedit-exec file "/bin/cp" (list file
			      copy)))

(defun suedit-exec (file command args)

  (let* ((file (file-truename file))
	 (entry (assq-equal file suedit-copies))
	 (copy (or (nth 1 entry)
		   (make-temp-file "Aquamacs")))
	 (auth-code (nth 2 entry)))
    
  (unless (and auth-code
	       (file-readable-p copy))

    (setq auth-code
	  (ns-eval-priv command
			args
			(or auth-code
			    t))))

  (unless entry
    (setq suedit-copies (cons 
			 (list file copy auth-code)
			 suedit-copies)))
  copy))

) ; when nil
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(defvar suedit-original-buffer-file nil)
(make-variable-buffer-local 'suedit-original-buffer-file)

(defun suedit-before-save ()
  (when buffer-file-name
    (let ((tmp (make-temp-file "Aquamacs")))
      (set (make-local-variable 'suedit-original-buffer-file)
	   buffer-file-name)
      (set-visited-file-name tmp))))

(defvar suedit-authorization-code nil)
(make-variable-buffer-local 'suedit-authorization-code)
(defun suedit-after-save ()
  (when suedit-original-buffer-file

    (setq suedit-authorization-code
	  (ns-eval-priv (expand-file-name "cp-preserve"
					  exec-directory) 
			(list buffer-file-name
			      suedit-original-buffer-file)
			(or suedit-authorization-code
			    t)))
    (set-visited-file-name suedit-original-buffer-file)
    (setq suedit-original-buffer-file nil)))


(defun find-file-privileged (file &optional wildcards)
  (interactive
   (find-file-read-args "Find file: "
                        (confirm-nonexistent-file-or-buffer)))
  (let ((tmp (make-temp-file "Aquamacs")))

    (ns-eval-priv "/bin/cp" (list file tmp))
    
    (set (make-local-variable 'suedit-original-buffer-file)
	 file)
    (find-file tmp)
    (set-visited-file-name file)
    (add-hook 'before-save-hook 'suedit-before-save 'append 'local)
    (add-hook 'after-save-hook 'suedit-after-save 'append 'local)
))