;;; bha-lsp.el --- Build Hotspot Analyzer LSP client for Emacs -*- lexical-binding: t; -*-

;; Copyright (C) 2026 Build Hotspot Analyzer Contributors

;; Author: Build Hotspot Analyzer Contributors
;; Version: 0.2.0
;; Package-Requires: ((emacs "27.1") (lsp-mode "8.0") (cl-lib "0.5"))
;; Keywords: languages, c, cpp, lsp, performance
;; URL: https://github.com/yourusername/build-hotspot-analyzer

;;; Commentary:

;; This package provides LSP client integration for the Build Hotspot Analyzer,
;; enabling C/C++ build time optimization suggestions directly in Emacs.
;;
;; Features:
;; - Analyze project build performance
;; - Display optimization suggestions
;; - Apply suggestions individually or in batch
;; - Atomic transactions with rollback support
;; - UUID-based operation tracking

;;; Code:

(require 'lsp-mode)
(require 'json)
(require 'cl-lib)

;; ============================================================================
;; Configuration Constants
;; ============================================================================

(defgroup bha-lsp nil
  "Build Hotspot Analyzer LSP client."
  :group 'lsp-mode
  :prefix "bha-lsp-")

(defcustom bha-lsp-server-path "bha-lsp"
  "Path to the bha-lsp language server executable."
  :type 'string
  :group 'bha-lsp)

(defcustom bha-lsp-auto-analyze nil
  "Automatically analyze project on file open."
  :type 'boolean
  :group 'bha-lsp)

(defcustom bha-lsp-auto-analyze-delay 2
  "Delay in seconds before auto-analyzing after opening a file."
  :type 'integer
  :group 'bha-lsp)

(defcustom bha-lsp-max-suggestions-display 100
  "Maximum number of suggestions to display in the buffer."
  :type 'integer
  :group 'bha-lsp)

(defcustom bha-lsp-min-confidence-threshold 0.0
  "Minimum confidence threshold for displaying suggestions (0.0 to 1.0)."
  :type 'float
  :group 'bha-lsp)

(defcustom bha-lsp-default-priority-filter 2
  "Default priority filter for apply all (0=High, 1=Medium, 2=Low)."
  :type 'integer
  :group 'bha-lsp)

(defconst bha-lsp--priority-labels '("HIGH" "MEDIUM" "LOW")
  "Labels for priority levels.")

(defconst bha-lsp--priority-faces '(error warning success)
  "Faces for priority levels.")

;; ============================================================================
;; State Variables
;; ============================================================================

(defvar bha-lsp--suggestions nil
  "Cached build optimization suggestions.")

(defvar bha-lsp--analysis-result nil
  "Cached analysis result.")

(defvar bha-lsp--last-backup-id nil
  "ID of the last backup created during apply operations.")

(defvar bha-lsp--operation-counter 0
  "Counter for generating unique operation IDs.")

;; ============================================================================
;; UUID Generation
;; ============================================================================

(defun bha-lsp--generate-uuid ()
  "Generate a RFC 4122 version 4 UUID."
  (format "%04x%04x-%04x-%04x-%04x-%04x%04x%04x"
          (random 65536) (random 65536)
          (random 65536)
          (logior (logand (random 65536) #x0fff) #x4000)
          (logior (logand (random 65536) #x3fff) #x8000)
          (random 65536) (random 65536) (random 65536)))

(defun bha-lsp--generate-operation-id (prefix)
  "Generate a unique operation ID with PREFIX."
  (cl-incf bha-lsp--operation-counter)
  (format "%s-%d-%s" prefix bha-lsp--operation-counter (bha-lsp--generate-uuid)))

;; ============================================================================
;; Bounds Checking Utilities
;; ============================================================================

(defun bha-lsp--safe-get (plist key &optional default)
  "Safely get KEY from PLIST, returning DEFAULT if not found or nil."
  (let ((value (plist-get plist key)))
    (if value value default)))

(defun bha-lsp--safe-number (value &optional default)
  "Ensure VALUE is a number, returning DEFAULT (or 0) if not."
  (if (numberp value) value (or default 0)))

(defun bha-lsp--safe-string (value &optional default)
  "Ensure VALUE is a string, returning DEFAULT (or empty string) if not."
  (if (stringp value) value (or default "")))

(defun bha-lsp--safe-priority (priority)
  "Ensure PRIORITY is valid (0-2), defaulting to 2 (Low)."
  (if (and (integerp priority) (>= priority 0) (<= priority 2))
      priority
    2))

(defun bha-lsp--safe-confidence (confidence)
  "Ensure CONFIDENCE is valid (0.0-1.0), defaulting to 0."
  (if (and (numberp confidence) (>= confidence 0.0) (<= confidence 1.0))
      confidence
    0.0))

(defun bha-lsp--valid-suggestion-p (sug)
  "Check if SUG is a valid suggestion plist."
  (and (listp sug)
       (stringp (plist-get sug :id))
       (> (length (plist-get sug :id)) 0)
       (stringp (plist-get sug :title))
       (numberp (plist-get sug :priority))
       (numberp (plist-get sug :confidence))))

(defun bha-lsp--filter-valid-suggestions (suggestions)
  "Filter SUGGESTIONS to only include valid ones."
  (if (vectorp suggestions)
      (cl-remove-if-not #'bha-lsp--valid-suggestion-p (append suggestions nil))
    (cl-remove-if-not #'bha-lsp--valid-suggestion-p suggestions)))

;; ============================================================================
;; LSP Command Execution
;; ============================================================================

(defun bha-lsp--execute-command (command &optional args)
  "Execute LSP command COMMAND with optional ARGS synchronously."
  (lsp-request "workspace/executeCommand"
               `(:command ,command
                 :arguments ,(or args []))))

(defun bha-lsp--execute-command-async (command args success-handler &optional error-handler)
  "Execute LSP command COMMAND with ARGS asynchronously.
Call SUCCESS-HANDLER on success, ERROR-HANDLER on error."
  (lsp-request-async
   "workspace/executeCommand"
   `(:command ,command :arguments ,args)
   success-handler
   :error-handler (or error-handler
                      (lambda (error)
                        (message "BHA command failed: %s"
                                 (bha-lsp--safe-string (plist-get error :message) "Unknown error"))))))

;; ============================================================================
;; Core Commands
;; ============================================================================

;;;###autoload
(defun bha-lsp-analyze-project (&optional build-dir)
  "Analyze project build performance, optionally using BUILD-DIR."
  (interactive
   (list (read-directory-name "Build directory (empty for auto-detect): "
                              nil nil nil)))
  (let* ((operation-id (bha-lsp--generate-operation-id "analyze"))
         (root-dir (lsp-workspace-root))
         (args (list (list :projectRoot root-dir
                          :buildDir (if (and build-dir (not (string-empty-p build-dir)))
                                       build-dir
                                     :json-null)
                          :rebuild :json-false
                          :operationId operation-id))))
    (unless root-dir
      (user-error "No LSP workspace root found"))
    (message "Analyzing project (operation: %s)..." operation-id)
    (bha-lsp--execute-command-async
     "bha.analyze" args
     (lambda (result)
       (setq bha-lsp--analysis-result result)
       (let* ((raw-suggestions (plist-get result :suggestions))
              (valid-suggestions (bha-lsp--filter-valid-suggestions raw-suggestions)))
         (setq bha-lsp--suggestions (vconcat valid-suggestions))
         (let ((num-suggestions (length bha-lsp--suggestions)))
           (message "Analysis complete: %d suggestions found" num-suggestions)
           (when (> num-suggestions 0)
             (bha-lsp-show-suggestions))))))))

;;;###autoload
(defun bha-lsp-show-suggestions ()
  "Display build optimization suggestions in a buffer."
  (interactive)
  (if bha-lsp--suggestions
      (bha-lsp--render-suggestions)
    (bha-lsp--execute-command-async
     "bha.showMetrics" []
     (lambda (result)
       (let* ((raw-suggestions (plist-get result :suggestions))
              (valid-suggestions (bha-lsp--filter-valid-suggestions raw-suggestions)))
         (setq bha-lsp--suggestions (vconcat valid-suggestions))
         (if (> (length bha-lsp--suggestions) 0)
             (bha-lsp--render-suggestions)
           (message "No suggestions available. Run M-x bha-lsp-analyze-project first")))))))

(defun bha-lsp--render-suggestions ()
  "Render suggestions in a dedicated buffer."
  (let ((buf (get-buffer-create "*BHA Suggestions*"))
        (suggestions bha-lsp--suggestions)
        (metrics (plist-get bha-lsp--analysis-result :baselineMetrics))
        (max-display bha-lsp-max-suggestions-display))
    (with-current-buffer buf
      (let ((inhibit-read-only t))
        (erase-buffer)
        (insert (propertize "Build Hotspot Analysis\n" 'face 'bold))
        (insert (make-string 60 ?=) "\n\n")

        ;; Metrics section
        (when metrics
          (insert (propertize "Build Metrics:\n" 'face 'bold))
          (let ((duration (bha-lsp--safe-number (plist-get metrics :totalDurationMs) 0))
                (files (bha-lsp--safe-number (plist-get metrics :filesCompiled) 0)))
            (insert (format "  Total Build Time: %.2fs\n" (/ (float duration) 1000)))
            (insert (format "  Files Compiled: %d\n\n" files))))

        ;; Action buttons
        (insert (propertize "Actions: " 'face 'bold))
        (insert-button "[Apply All]"
                       'action (lambda (_) (bha-lsp-apply-all-suggestions))
                       'follow-link t
                       'help-echo "Apply all suggestions")
        (insert " ")
        (insert-button "[Apply Safe]"
                       'action (lambda (_) (bha-lsp-apply-all-suggestions t))
                       'follow-link t
                       'help-echo "Apply only auto-applicable suggestions")
        (insert " ")
        (insert-button "[Revert]"
                       'action (lambda (_) (bha-lsp-revert-changes))
                       'follow-link t
                       'help-echo "Revert last applied changes")
        (insert "\n\n")

        ;; Suggestions
        (let ((display-count (min (length suggestions) max-display)))
          (insert (propertize (format "Optimization Suggestions (%d of %d):\n"
                                      display-count (length suggestions))
                              'face 'bold))
          (insert (make-string 60 ?-) "\n\n")

          (cl-loop for i from 0 below display-count
                   for sug = (aref suggestions i)
                   for idx = (1+ i)
                   do
                   (let* ((title (bha-lsp--safe-string (plist-get sug :title) "Untitled"))
                          (desc (bha-lsp--safe-string (plist-get sug :description) ""))
                          (priority (bha-lsp--safe-priority (plist-get sug :priority)))
                          (confidence (bha-lsp--safe-confidence (plist-get sug :confidence)))
                          (auto-applicable (plist-get sug :autoApplicable))
                          (impact (plist-get sug :estimatedImpact))
                          (id (plist-get sug :id))
                          (priority-str (nth priority bha-lsp--priority-labels))
                          (priority-face (nth priority bha-lsp--priority-faces)))

                     (insert (format "[%d] " idx))
                     (insert (propertize title 'face 'bold))
                     (when auto-applicable
                       (insert (propertize " [Auto]" 'face 'success)))
                     (insert "\n")

                     (insert (format "    Priority: %s | Confidence: %d%%\n"
                                     (propertize priority-str 'face priority-face)
                                     (floor (* confidence 100))))

                     (insert (format "    %s\n" desc))

                     (when impact
                       (let ((time-saved (bha-lsp--safe-number (plist-get impact :timeSavedMs) 0))
                             (percentage (bha-lsp--safe-number (plist-get impact :percentage) 0))
                             (files-affected (bha-lsp--safe-number (plist-get impact :filesAffected) 0)))
                         (insert (format "    Impact: %.2fs (%.1f%%) | %d files affected\n"
                                         (/ (float time-saved) 1000)
                                         percentage
                                         files-affected))))

                     (insert "    ")
                     (insert-button "[Apply]"
                                    'action `(lambda (_)
                                              (bha-lsp-apply-suggestion ,id))
                                    'follow-link t
                                    'help-echo "Apply this suggestion")
                     (insert "\n\n"))))

        (when (> (length suggestions) max-display)
          (insert (format "\n... and %d more suggestions (increase bha-lsp-max-suggestions-display to see more)\n"
                          (- (length suggestions) max-display))))

        (insert (make-string 60 ?=) "\n")
        (insert "Keys: 'q' close, 'r' refresh, 'a' analyze, 'A' apply all, 'R' revert\n"))

      (goto-char (point-min))
      (bha-lsp-suggestions-mode))
    (display-buffer buf)))

;;;###autoload
(defun bha-lsp-apply-suggestion (suggestion-id)
  "Apply suggestion with SUGGESTION-ID."
  (interactive
   (list (if bha-lsp--suggestions
             (let* ((choices (cl-loop for i from 0 below (length bha-lsp--suggestions)
                                      for sug = (aref bha-lsp--suggestions i)
                                      when (bha-lsp--valid-suggestion-p sug)
                                      collect (cons (format "[%s] %s - %s"
                                                            (nth (bha-lsp--safe-priority (plist-get sug :priority))
                                                                 bha-lsp--priority-labels)
                                                            (bha-lsp--safe-string (plist-get sug :title) "Untitled")
                                                            (bha-lsp--safe-string (plist-get sug :description) ""))
                                                    (plist-get sug :id))))
                    (choice (completing-read "Apply suggestion: " choices nil t)))
               (cdr (assoc choice choices)))
           (user-error "No suggestions available. Run M-x bha-lsp-analyze-project first"))))

  ;; Validate suggestion ID
  (unless (and suggestion-id (stringp suggestion-id) (> (length suggestion-id) 0))
    (user-error "Invalid suggestion ID"))

  (when (yes-or-no-p "Apply this suggestion? This will modify your code. ")
    (let ((operation-id (bha-lsp--generate-operation-id "apply")))
      (message "Applying suggestion (operation: %s)..." operation-id)
      (bha-lsp--execute-command-async
       "bha.applySuggestion"
       (vector (list :suggestionId suggestion-id :operationId operation-id))
       (lambda (result)
         (if (eq (plist-get result :success) t)
             (let ((num-files (length (or (plist-get result :changedFiles) [])))
                   (backup-id (plist-get result :backupId)))
               (when backup-id
                 (setq bha-lsp--last-backup-id backup-id))
               (message "Suggestion applied successfully. Modified %d files." num-files)
               (revert-buffer t t t))
           (let* ((errors (plist-get result :errors))
                  (error-msg (if (and errors (> (length errors) 0))
                                 (bha-lsp--safe-string (plist-get (aref errors 0) :message) "Unknown error")
                               "Unknown error")))
             (message "Failed to apply suggestion: %s" error-msg))))))))

;;;###autoload
(defun bha-lsp-apply-all-suggestions (&optional safe-only)
  "Apply all suggestions atomically.
If SAFE-ONLY is non-nil, only apply auto-applicable suggestions."
  (interactive "P")
  (unless bha-lsp--suggestions
    (user-error "No suggestions available. Run M-x bha-lsp-analyze-project first"))

  (let* ((valid-suggestions (bha-lsp--filter-valid-suggestions bha-lsp--suggestions))
         (filtered-count (if safe-only
                             (cl-count-if (lambda (s) (plist-get s :autoApplicable)) valid-suggestions)
                           (length valid-suggestions))))

    (when (= filtered-count 0)
      (user-error "No suggestions match the criteria"))

    (when (yes-or-no-p (format "Apply %d %ssuggestions? This will modify your code. A backup will be created. "
                               filtered-count
                               (if safe-only "auto-applicable " "")))
      (let ((operation-id (bha-lsp--generate-operation-id "apply-all")))
        (message "Applying %d suggestions atomically (operation: %s)..." filtered-count operation-id)
        (bha-lsp--execute-command-async
         "bha.applyAllSuggestions"
         (vector (list :minPriority bha-lsp-default-priority-filter
                       :safeOnly (if safe-only t :json-false)
                       :atomic t
                       :operationId operation-id))
         (lambda (result)
           (let ((success (plist-get result :success))
                 (applied-count (bha-lsp--safe-number (plist-get result :appliedCount) 0))
                 (skipped-count (bha-lsp--safe-number (plist-get result :skippedCount) 0))
                 (failed-count (bha-lsp--safe-number (plist-get result :failedCount) 0))
                 (backup-id (plist-get result :backupId)))
             (when backup-id
               (setq bha-lsp--last-backup-id backup-id))
             (if success
                 (progn
                   (message "Applied %d suggestions successfully. Skipped: %d."
                            applied-count skipped-count)
                   (revert-buffer t t t))
               (let* ((errors (plist-get result :errors))
                      (error-summary (if (and errors (> (length errors) 0))
                                         (format " First error: %s"
                                                 (bha-lsp--safe-string
                                                  (plist-get (aref errors 0) :message)
                                                  "Unknown"))
                                       "")))
                 (message "Apply all failed. %d errors.%s Changes rolled back."
                          failed-count error-summary))))))))))

;;;###autoload
(defun bha-lsp-revert-changes ()
  "Revert changes from the last apply operation."
  (interactive)
  (unless bha-lsp--last-backup-id
    (user-error "No backup available to revert"))

  (when (yes-or-no-p "Revert all changes from the last apply operation? ")
    (let ((operation-id (bha-lsp--generate-operation-id "revert")))
      (message "Reverting changes (operation: %s)..." operation-id)
      (bha-lsp--execute-command-async
       "bha.revertChanges"
       (vector (list :backupId bha-lsp--last-backup-id :operationId operation-id))
       (lambda (result)
         (if (eq (plist-get result :success) t)
             (let ((num-files (length (or (plist-get result :restoredFiles) []))))
               (setq bha-lsp--last-backup-id nil)
               (message "Reverted successfully. Restored %d files." num-files)
               (revert-buffer t t t))
           (let* ((errors (plist-get result :errors))
                  (error-msg (if (and errors (> (length errors) 0))
                                 (bha-lsp--safe-string (plist-get (aref errors 0) :message) "Unknown error")
                               "Unknown error")))
             (message "Failed to revert: %s" error-msg))))))))

;; ============================================================================
;; Mode Definition
;; ============================================================================

(defvar bha-lsp-suggestions-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "q") 'quit-window)
    (define-key map (kbd "r") 'bha-lsp-show-suggestions)
    (define-key map (kbd "a") 'bha-lsp-analyze-project)
    (define-key map (kbd "A") 'bha-lsp-apply-all-suggestions)
    (define-key map (kbd "R") 'bha-lsp-revert-changes)
    (define-key map (kbd "g") 'bha-lsp-show-suggestions)
    map)
  "Keymap for BHA suggestions buffer.")

(define-derived-mode bha-lsp-suggestions-mode special-mode "BHA-Suggestions"
  "Major mode for displaying BHA build optimization suggestions.

\\{bha-lsp-suggestions-mode-map}"
  (setq buffer-read-only t))

;; ============================================================================
;; LSP Client Registration
;; ============================================================================

(lsp-register-client
 (make-lsp-client
  :new-connection (lsp-stdio-connection
                   (lambda () (list bha-lsp-server-path)))
  :major-modes '(c-mode c++-mode objc-mode)
  :priority -1
  :server-id 'bha-lsp
  :activation-fn (lsp-activate-on "c" "cpp" "objective-c")))

;;;###autoload
(defun bha-lsp-setup ()
  "Setup BHA LSP integration."
  (interactive)
  (add-hook 'c-mode-hook #'lsp-deferred)
  (add-hook 'c++-mode-hook #'lsp-deferred)

  (when bha-lsp-auto-analyze
    (add-hook 'lsp-after-open-hook
              (lambda ()
                (when (member major-mode '(c-mode c++-mode))
                  (run-with-timer bha-lsp-auto-analyze-delay nil #'bha-lsp-analyze-project))))))

(provide 'bha-lsp)

;;; bha-lsp.el ends here
