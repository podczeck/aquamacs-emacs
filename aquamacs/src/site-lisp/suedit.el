emacs tries to create various help files e.g. .dir-locals.el  [this should probably be fixed in Aquamacs].
but also auto-save files etc.
all of this will fail in the root-only directory.

perhaps we should recreate the directory structure in a temporary directory or in ~/Library

/tmp/503/Aquamacs/etc/apache/httpd.conf 

these are all only accessible by the current user.

create when opening


alas, C-x C-f file completion doesn't work without root access.





(defvar suedit-copies nil)

(when nil
;; put file name in handler

(add-to-list 'file-name-handler-alist
	     '("/tmp/xxx1" . suedit-handler))

) ; when nil


;; contains list of (file copy auth-code)


(defun suedit-handler (fun &rest args)
  (let ((file-name-handler-alist nil))
  
    ;; is there a copy?
    ;; if so, check to make sure it is 
    ;; (print (format "external: fun %s %s" fun args))
    (cond
     ((memq fun '(insert-file-contents))
      ;; first arg MUST be filename
      ;; use the copy instead of buffer-file-name
      ;; we assume that the buffer is current.
      (apply fun (cons (suedit-update-copy (car args)) (cdr args)))
      (if (nth 1 args) ;; visit?
	  (set-visited-file-name (car args) 'no-query)))
     ((memq fun '(write-region))
      ;; first arg MUST be filename
      ;; use the copy instead of buffer-file-name
      ;; we assume that the buffer is current.
      (let ((file (nth 2 args)))
	(setf (nth 2 args) (suedit-update-copy file))
	(apply fun args)
	;; copy back
	(suedit-restore-copy file)
	(set-visited-file-name file 'no-query)))
     (t
      ;; call emacs process
      ;; maybe convert file name to file-truename??
      (let ((code
	     (let ((print-level nil) (print-length nil))
	       (format "%S" (cons fun args)))))
	
	(condition-case nil
	      
	    (car (read-from-string 
		  (with-temp-buffer
		    (suedit-exec (car args)
			     (format "%s../Aquamacs" exec-directory) ;; todo: fix me
			     (list  "-batch" "-Q" "-eval" (format "(print %s)" code)))
		    (buffer-string))))
	  (error nil))
	))
    )))

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

 (defun suedit-restore-copy (file)

  (let* ((file (file-truename file))
	 (entry (assoc file suedit-copies))
	 (copy (nth 1 entry))
	 (auth-code (nth 2 entry)))
    
  (if (and copy
	   (file-readable-p copy))

    (setq auth-code
	  (ns-exec-priv "/bin/cp"
 			(list copy file)
			(or auth-code
			    t))))

  (if entry
      (setf (nth 2 (assoc file suedit-copies)) auth-code)
    )
))
(defun suedit-update-copy (file)
  (let* ((file (file-truename file))
	 (entry (assoc file suedit-copies))
	 (copy (or (nth 1 entry)
		   (make-temp-file "Aquamacs")))
	 (auth-code (nth 2 entry)))
    
  (unless (and auth-code
	       (file-readable-p copy))


    (setq auth-code
	  (ns-exec-priv "/bin/cp"
			(list file
			      copy)
			(or auth-code
			    t))))

  (if entry
      (setf (nth 2 (assoc file suedit-copies)) auth-code)
    (setq suedit-copies (cons 
			 (list file copy auth-code)
			 suedit-copies)))
  copy))

(defun suedit-exec (file command args)
  (let* ((file (file-truename file))
	 (entry (assoc file suedit-copies))
	 (copy (or (nth 1 entry)
		   (make-temp-file "Aquamacs")))
	 (auth-code (nth 2 entry)))
    

    (setq auth-code
	  (ns-exec-priv command
			args
			(or auth-code
			    t)))

  (if entry
      (setf (nth 2 (assoc file suedit-copies)) auth-code)
    (setq suedit-copies (cons 
			 (list file copy auth-code)
			 suedit-copies)))
  ))

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
	  (ns-exec-priv (expand-file-name "cp-preserve"
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

    (ns-exec-priv "/bin/cp" (list file tmp))
    
    (set (make-local-variable 'suedit-original-buffer-file)
	 file)
    (find-file tmp)
    (set-visited-file-name file)
    (add-hook 'before-save-hook 'suedit-before-save 'append 'local)
    (add-hook 'after-save-hook 'suedit-after-save 'append 'local)
))

(defvar auth t)
(setq auth t)
(when nil
;; (let ((code '))
;;   (suedit-exec "test" "emacs" (list nil t nil "-batch" "-Q" "-eval" (format "(print %s)" code))))

(car (read-from-string 
(with-temp-buffer
  (setq auth (ns-exec-priv (format "%s../Aquamacs" exec-directory)
	     '("-batch" "-Q" "-eval" "(print (expand-file-name \"/tmp/xxx1\" nil))")
	     auth))
  (buffer-string))))


(setq auth t)
(setq count 0)
(setq coll nil)
(dotimes (n 50)
(with-temp-buffer
  (setq auth (ns-exec-priv (format "%s../Aquamacs" exec-directory)
	     (list "-batch" "-Q" "-eval" "(print (substitute-in-file-name \"/tmp/xxx1\"))")
	     auth))
  (setq coll (cons (buffer-string) coll))
  (setq count (+ count (buffer-size)))))


(setq auth (ns-exec-priv "/bin/echo"
	     (list "/tmp/t4")
auth))

(setq auth (ns-exec-priv "/bin/sleep"
	     (list "3")
auth))

(with-temp-buffer
  (ns-exec-priv "/bin/cat"
	      (list "/tmp/ACT-UP.html")
	      auth)
  (goto-char 0)
  (read (current-buffer)))


(call-process (format "%s../Aquamacs" exec-directory) nil nil nil
	     "-batch" "-Q" "-eval" "(make-directory \"/tmp/t66\")")
)


