package com.nevo.voip.feature.settings.ui

import androidx.lifecycle.ViewModel
import com.nevo.voip.core.update.UpdateManager
import dagger.hilt.android.lifecycle.HiltViewModel
import javax.inject.Inject

@HiltViewModel
class UpdateViewModel @Inject constructor(
    val updateManager: UpdateManager
) : ViewModel()